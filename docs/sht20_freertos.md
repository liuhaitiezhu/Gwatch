# SHT20 采集、消息队列与串口打印

## 接线

沿用工程现有外设，不需要修改 `.ioc`：

| 信号 | STM32U575 引脚 / 参数 |
| --- | --- |
| SHT20 SCL | PB6，I2C1_SCL |
| SHT20 SDA | PB7，I2C1_SDA |
| SHT20 电源 | 3.3 V、GND；与开发板共地 |
| I²C 上拉 | SDA、SCL 需要外部上拉到 3.3 V；先确认模块已有上拉 |
| 串口 TX | PA9，接 USB 转串口模块的 RX |
| 串口 RX | PA10，本示例不需要接收数据 |
| 串口参数 | USART1，115200，8 数据位，无校验，1 停止位 |

SHT20 的 **7 位地址为 0x40**，传给 STM32 HAL 的地址为 `0x40 << 1`。
触摸芯片 FT6336 地址为 0x38，可以共用这条总线。

## 文件和调用关系

- `Core/Src/bsp_sht20.c` / `Core/Inc/bsp_sht20.h`：传感器初始化、温湿度读取、CRC 校验、单位换算。
- `Core/Src/bsp_i2c_bus.c` / `Core/Inc/bsp_i2c_bus.h`：共享 I2C1 互斥锁、SHT20 命令发送。
- `Core/Src/app_freertos.c` / `Core/Inc/app_freertos.h`：创建队列和任务、启动调度器、串口日志入口、内存失败和栈溢出钩子。
- `Core/Src/main.c`：初始化底层外设和 TouchGFX 框架资源，再调用无参数的 `App_FreeRTOS_Start()`；不再运行显示、触摸循环。
- `Core/Src/bsp_ft6336.c`：I²C 读写增加同一把互斥锁，其余触摸识别逻辑保留。
- 根目录 `CMakeLists.txt`：加入三个新源文件。

```text
SHT20_Task ── SHT20_Init / SHT20_Read ── xQueueSend ─┐
                                                  ├─ uart_queue ─ xQueueReceive ─ UART_PrintTask ─ USART1
显示/触摸任务日志 ─ printf ─ _write ─ App_UART_Write ─┘
```

队列长度为 8，队列元素是 `UART_Message` 结构体，可以携带一组采样结果或最多 128 字节的日志。
队列复制消息内容，不保存局部变量的地址。采样失败时传递错误状态，不输出旧温湿度。
串口任务阻塞等待消息；队列繁忙时采集任务等待可用空间，普通日志最多等待 100 ms。
串口实际发送也设置 100 ms 超时。只有串口任务操作 USART1 发送；`App_UART_Write()` 只接受运行中的任务调用，调度器启动前不再直接发送日志。

| 任务名 | 优先级 | 栈大小 | 行为 |
| --- | --- | --- | --- |
| `sht20` | 2 | 512 words / 2 KiB | 默认每 2000 ms 采集一次，失败后下一周期重新初始化 |
| `uart_print` | 2 | 768 words / 3 KiB | 阻塞接收消息，格式化并输出 |
| `display_touch` | 1 | 2048 words / 8 KiB | 在任务内初始化 LCD、触摸，随后扫描触摸并处理 TouchGFX，每轮延时 1 ms |

三个任务分别由 `UART_PrintTask()`、`SHT20_Task()` 和 `DisplayTouchTask()` 直接执行。
不再通过 `Application_Process` 或函数指针回调 main 中的业务循环。
显示任务先初始化 LCD，再初始化共享 PA8 RESET 的触摸芯片；完成后清除 TE 待处理中断，
开启 PA11 中断。LCD 初始化失败时保留触摸扫描和传感器、串口任务，跳过 GUI 更新。
硬件中断回调也由 `app_freertos.c` 接入，TE 继续使用准备好画面后等待新下降沿的发送流程。

TouchGFX 的生成入口仍通过当前帧同步标志判断是否需要处理一帧；该检查只由显示任务执行。
`main` 中的应用轮询已删除。CubeMX 再次生成代码后可能重新加入 `main` 的默认循环，
但 `App_FreeRTOS_Start()` 正常不会返回，启动失败则由紧随其后的 `Error_Handler()` 处理。
任务延时不会让 MCU 进入低功耗模式。HAL 时基继续由 TIM6 提供，FreeRTOS 使用 SysTick。

## 驱动行为

依据 [Sensirion SHT20 数据手册](https://sensirion.com/media/documents/CCDE1377/635000A2/Sensirion_Datasheet_Humidity_Sensor_SHT20.pdf)：

- 初始化执行软复位，并通过读改写用户寄存器保持保留位、关闭加热器，选择 14 位温度 / 12 位湿度。
- 使用 no-hold 命令 `0xF3`、`0xF5`，转换等待分别为至少 90 ms、30 ms。等待期间释放 I²C 互斥锁，触摸仍可通信。
- SHT20 接口及 I²C 总线锁只用于调度器启动后的任务上下文；转换等待统一使用 `vTaskDelay()`，已删除裸机延时和免锁兼容分支。
- 命令发送使用 STM32 LL 的 SOFTEND，命令 ACK 后用 TIM6 等待至少 1 ms 再产生 STOP，满足手册建议的 20 μs 间隔；接收数据使用 HAL。
- 每次读取 2 字节数据和 1 字节 CRC，按多项式 `0x31`、初值 `0x00` 校验，再检查测量类型并屏蔽状态位。
- 温度、湿度以百分之一单位的整数保存，避免依赖浮点 `printf`。湿度限制在 0～100%RH。

修改采样周期：`Core/Src/app_freertos.c` 的 `SHT20_SAMPLE_PERIOD_MS`。
修改任务栈和优先级：同文件 `App_FreeRTOS_Start()` 内的 `xTaskCreate()`。
`configCHECK_FOR_STACK_OVERFLOW=2`、`configUSE_MALLOC_FAILED_HOOK=1` 已启用；相应钩子可设置断点。
可在调试器中使用 `uxTaskGetStackHighWaterMark()` 和 `xPortGetFreeHeapSize()` 检查实际余量。

## 验证

```powershell
# 固件编译，使用工程的 ARM GCC 工具链
cmake --build --preset Debug --parallel 2

# 主机模拟测试，需要原生 Windows GCC
./tests/sht20/run.ps1 -Compiler C:/mingw64/bin/gcc.exe
```

主机测试编译真实驱动和任务模块，模拟 HAL、I²C 传输与 RTOS 队列：检查已知 CRC 向量、负温度、湿度边界、超时、无效数据、失败重试、队列复制和串口输出，并检查显示初始化在任务内执行、共享复位的初始化顺序、TE 启用条件以及初始化失败时跳过 GUI。
测试不模拟真实 Cortex-M 任务切换，也不验证 I²C 电气时序；I²C 命令底层仍需上板验证。

上板后串口输出示例（数值仅为示例）：

```text
[SHT20] #1 t=1500 ms T=24.69 C RH=42.49 %
[SHT20] #2 t=3500 ms T=24.71 C RH=42.50 %
```

未接传感器时会周期性报告 I²C 错误并重试。检查输出周期及读数，同时操作触摸屏，确认共用总线后触摸与显示仍正常。
