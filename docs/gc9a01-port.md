# GC9A01 显示驱动移植

来源：本地 `GC9A01-Driver` 分支，提交
`822b3f07e5a18e506a07ad1e92587f6fd059b497`。
仅移植显示驱动及其测试，保留当前 `manual` 分支的 FreeRTOS、TouchGFX、
界面资源、FT6336 和 SHT20 代码。未合并来源分支的独立三色刷屏主程序。

## 接线和传输方式

| 信号 | 引脚 |
| --- | --- |
| CS | PA4，驱动运行时配置为 GPIO 输出 |
| DC | PB0 |
| SCK | PA5 |
| SDA | PA12 |
| RESET | PA8 |
| 背光 | PB8，高电平点亮 |

物理显示范围为 240×240，像素格式为 RGB565。

`GWATCH_LCD_SOFTWARE_SPI` 默认 `ON`：使用来源分支已在本板验证能显示三色的
GPIO SPI；PA12 每次电平变化后，使用现有 1 MHz TIM6 计数器等待至少 20 µs。
这是兼容核心板 PA12 上 C20（100 nF）的当前工作方式，整屏刷新可能需要几秒。
不修改 TIM6、IRQ、系统时钟或 `.ioc`。

驱动也保留硬件 SPI 路径以及原有的 SPI1 /4、40 MHz 配置。
当前 PCB 的 PA12 电容负载尚未处理，应继续保持上述选项为 `ON`。
硬件路径使用 8 位指令、16 位像素传输，像素高字节先发，传输完成或失败时
恢复 8 位配置并释放 CS。

## 当前调用关系

1. `main.c` 保留原有外设初始化和 `MX_TouchGFX_Init()`，随后启动 FreeRTOS。
2. `DisplayTouchTask()` 调用 `GC9A01_Init()`，初始化成功后开启背光。
3. 原有 GUI 定时器继续每 16 ms 通知显示任务；慢速传输后合并积累的通知。
4. `TouchGFXHAL::flushFrameBuffer()` 收集脏矩形，`endFrame()` 完成绘制后
   调用 `GC9A01_WriteRectRGB565()`，使用 TouchGFX 的实际 framebuffer stride。
5. 显示任务在初始化原有触摸服务后注册传输回调。软件 SPI 每个完整像素后、
   硬件 SPI 每个数据块后调用原有 `FT6336_scan_task()`，保持其 IRQ 和 20 ms
   补扫判断有机会在慢速传输期间执行。回调与 GUI 属于同一个任务，不能再次调用 LCD。

保留原有任务、通知方式和 IRQ 优先级。没有移植 CST816D 触摸驱动；现有 FT6336
代码的保留不代表其与新模块触摸芯片兼容或已经通过实物验证。

## TouchGFX 尺寸

在可手工维护的 `TouchGFXHAL.hpp` 构造函数中，将运行时 HAL 和 framebuffer
尺寸指定为 `GC9A01_WIDTH/HEIGHT`（240×240），并将当前页面滚动容器的可视范围
同步为这个尺寸。方屏会被 HAL 默认识别为横屏，但当前 Designer 页面要求竖屏；
因此同时将原生方向设为 `ORIENTATION_PORTRAIT`，避免页面切换触发不需要的
`rotate90`，确保未旋转的图片、字体和屏幕坐标保持一致。

移植期间未修改 `.ioc` 或自动生成文件。用户随后更新了 CubeMX/Designer 的
屏幕尺寸，当前生成代码已分配 240×240 帧缓冲；这些更新均予以保留。
后续在 Designer 中调整界面时，仍需按物理圆屏的可视区域检查布局。

## 驱动接口

`Core/Inc/bsp_gc9a01.h` 提供：

- `GC9A01_Init()`：复位、完整初始化序列、清黑屏，背光保持关闭。
- `GC9A01_SetBacklight()`：开关背光。
- `GC9A01_Refresh()`：发送连续的 240×240 RGB565 帧缓冲。
- `GC9A01_WriteRectRGB565()`：按矩形及源 stride 刷新。
- `GC9A01_FillRect()`、`GC9A01_Clear()`：区域填色、整屏填色。
- `GC9A01_SetTransferCallback()`：可选的任务内协作服务，传入 `NULL` 关闭。

所有绘图接口均为阻塞调用，只能由显示任务单独使用。调用者必须保留源缓冲区
直至返回，不能从 ISR 调用。旧的 `bsp_ili9341_4line.c/.h` 已删除，编译目标及
当前源码的显示调用均已替换。

## 验证

- `C:\Python314\python.exe tests/run_gc9a01_tests.py`：两种传输路径均通过。
  覆盖初始化、RGB565 字节序、stride、完整帧、区域填色、越界和状态检查、
  硬件传输失败恢复，以及软件 SPI 的 GPIO 解码、20 µs 等待和计数器回绕。
  同时验证传输回调在像素边界执行，期间 CS 保持有效。
- 硬件 SPI 分支已使用实际 STM32U5 HAL 和交叉编译器单独编译通过。
- `cmake --preset Debug`、`cmake --build --preset Debug --parallel 4`：通过。
- `cmake --preset Release`、`cmake --build --preset Release --parallel 4`：通过。
- 构建产物：`build/Debug/spi_demo.elf/.hex/.bin` 和对应 Release 目录。
- 构建预设沿用来源分支的 STM32 GCC PATH 设置，避免误用主机汇编器。
- 用户更新屏幕尺寸后，以新的 `spi_demo.ioc` 为保留基准，SHA-256 为
  `85a063630b77a1d47f7f41291f0b7c0f84c1fe03e5d863d3e0aa292aeb5fdccd`。

## 板上验证

已通过同一块 ST-LINK 板子执行以下检查：

- 下载前备份完整 2 MB Flash，位于
  `C:\Users\1\AppData\Local\Temp\gwatch-gc9-branch-port-m37_jbw9\pre-port-full-flash.bin`。
- 最新 Debug 固件已下载、校验并复位运行。
- 显示驱动和显示任务的 ready 标志均为 1，GUI 定时器和处理计数持续增加。
- 运行时显示尺寸、帧缓冲尺寸均为 240×240，`DISPLAY_ROTATION` 为 `rotate0`。
- 从 MCU 读回 115200 字节 RGB565 帧缓冲并渲染检查，图片和文字布局正常。
  排除温度文字和滚动条后，47560 个图片像素均与原始 PNG 匹配，
  容许每个 RGB565 通道一档量化误差。
- 方向修正后的固件下载后，用户已确认实物屏幕正常显示。

初版集成时，方屏默认横屏与页面竖屏设置不一致，导致 `rotate90` 和图像错位；
现已在手工维护的 HAL 构造函数中修正，并通过帧缓冲读回及用户实物观察确认。
此确认针对当前软件 SPI 路径的屏幕显示，不包含硬件 SPI 或 CST816D 触摸验证。
