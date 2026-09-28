# spi_demo 工程长期笔记

STM32U575 + 2.8 寸 ILI9341（4 线 SPI）+ TouchGFX 4.26.1 的图形工程。
路径：`C:\Users\1\Desktop\CubeMx_Project\spi_demo`

## 文件归属分层（改代码前先看这个）

### A 层 · 绝对不要动（工具链/厂商提供）
| 目录 | 规模 | 内容 |
|---|---|---|
| `Drivers/` | 97 文件 / 9.1 MB | ST HAL + CMSIS，CubeMX 按启用的外设挑选 |
| `Middlewares/ST/touchgfx/` | 550 文件 / 117 MB | TouchGFX 框架本体 + 预编译库，Designer 自动拷贝 |

### B 层 · 生成物，别手改（重新生成会覆盖）
- `spi_demo.ioc`、`.mxproject`、`.settings/`、`Core/Inc/RTE_Components.h`
- `cmake/stm32cubemx/CMakeLists.txt`（CubeMX）、`cmake/touchgfx/CMakeLists.txt`（**Designer 生成，文件头写明 Do not modify**）、`CMakePresets.json`、`cmake/gcc-arm-none-eabi.cmake`、`cmake/starm-clang.cmake`
- `startup_stm32u575xx.s`、`STM32U575xx_FLASH.ld`、`STM32U575xx_RAM.ld`
- `Core/Src/`：`gpio.c` `spi.c` `usart.c` `crc.c` `stm32u5xx_hal_msp.c` `stm32u5xx_it.c` `syscalls.c` `sysmem.c` `system_stm32u5xx.c`
- `Core/Inc/`：对应头文件 + `main.h` + `stm32u5xx_hal_conf.h`
- `TouchGFX/App/`、`TouchGFX/target/generated/`、`TouchGFX/generated/`、`TouchGFX/config/`

### C 层 · 有 USER CODE 保留区，只在区内写
- `Core/Src/main.c`
- `TouchGFX/target/TouchGFXHAL.cpp` / `.hpp`（显示输出适配 —— `flushFrameBuffer` 要写在这里）
- `TouchGFX/target/STM32TouchController.cpp` / `.hpp`（触摸适配）
- `TouchGFX/target/TouchGFXGPIO.cpp`

### D 层 · 自己的代码，随便改
| 路径 | 说明 |
|---|---|
| `Core/Src/bsp_ili9341_4line.c` + `Core/Inc/bsp_ili9341_4line.h` | ILI9341 驱动（从华清远见 51 例程移植，**没有 USER CODE 区，是纯用户文件**） |
| 根 `CMakeLists.txt` | 文件头写明 "User is free to modify the file as much as necessary"；新源文件注册在这里的用户区 |
| `assets/` | 取模素材（yuexinmao_01~16.png / .gif / array/ / jpg/） |
| `TouchGFX/gui/src/**`、`TouchGFX/gui/include/**` | 界面逻辑（Designer 生成骨架，业务逻辑自己加） |

### E 层 · 在 GUI 里操作，不是改文件
- **TouchGFX Designer**：打开 `TouchGFX/spi_demo.touchgfx` → 拖控件/加图/加字体 → Generate Code
  - 会更新 `TouchGFX/generated/`、`TouchGFX/assets/`、`cmake/touchgfx/CMakeLists.txt`
- **CubeMX**：打开 `spi_demo.ioc` → 改外设/引脚/时钟 → 重新生成

### 其他
- `build/` 构建产物（可删）；`.git/`；`.vscode/`；`.clangd`；`openocd.cfg`
- 根目录 `image.png`、两个 PDF 是参考资料

## 构建

```bash
cmake --preset Debug      # 或 VS Code 直接 Build
cmake --build build/Debug
```
- 工具链两套都可用：VS Code 用 `C:\Users\1\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\`；纯命令行 preset 用 `C:/arm-gnu-toolchain/`
- **工程不生成 `.bin`**（无 objcopy 规则），`build/Debug/spi_demo.bin` 是 7/23 的旧文件；烧录用 `.elf`
- 对象路径：顶层 `CMakeFiles/spi_demo.dir/`、`cmake/stm32cubemx/CMakeFiles/STM32_Drivers.dir/`、`cmake/touchgfx/CMakeFiles/TouchGFX.dir/`

## TouchGFX 目录内部分组（`TouchGFX/` 113 文件）
| 分组 | 路径 | 能否改 |
|---|---|---|
| 生成物 | `generated/`（72 文件，含 fonts/gui_generated/images/texts/videos/simulator + `cache/*.cache` 增量缓存） | ❌ 名字带 generated/cache 的都别动 |
| 生成物 | `App/`、`target/generated/` | ❌ CubeMX 管 |
| **你的代码** | `target/TouchGFXHAL.cpp/.hpp`、`target/STM32TouchController.cpp/.hpp`、`target/TouchGFXGPIO.cpp` | ✅ 有 USER CODE 区 |
| **你的代码** | `gui/include/gui/**`、`gui/src/**`（MVP：common/model/screen_screen） | ✅ **整个都是你的**，无 USER CODE 标记，Designer 只在增删 Screen 时生成 |
| 素材 | `assets/images/`（根目录空，只有 `__generated/` 放内置主题按钮图）、`assets/fonts/verdana.ttf`、`assets/texts/texts.xml`（只有空 Group1；已定义 Default=20/Large=40/Small=10 三个字号） | ⚠️ 在 Designer 里改，不手改 XML |
| 不碰 | `config/{gcc,msvs}`（CMake 工程用不到）、`simulator/` + `generated/simulator/`（PC 模拟器，不参与 MCU 构建） | ❌ |
| 顶层 | `spi_demo.touchgfx`（★双击开 Designer）、`application.config`、`target.config`（内含 `"toolchain":"CMake"`）、`ApplicationTemplate.touchgfx.part` | ❌ Designer 管 |

### `target/` 里真正必修的只有 3 个函数体
1. `TouchGFXHAL::flushFrameBuffer(const touchgfx::Rect& rect)` —— 显示输出（不写=黑屏）
2. `STM32TouchController::init()` —— 触摸初始化
3. `STM32TouchController::sampleTouch(int32_t& x, int32_t& y)` —— 查询触摸，有触摸返回 true 并填 x/y（默认每 tick 调一次，可用 `HAL::setTouchSampleRate()` 调）

**其余 10 个方法是"可覆写非必须"**，默认已能用：
`initialize` / `configureInterrupts` / `enableInterrupts` / `disableInterrupts` / `enableLCDControllerInterrupt`（这些是给 LTDC/GPU2D 中断用的，SPI 屏没这些中断 → 留空）、`beginFrame` / `endFrame` / `blockCopy` / `getTFTFrameBuffer` / `setTFTFrameBuffer`、以及无参 `flushFrameBuffer()`（在 .hpp 里内联转发到 Rect 版）。
`TouchGFXGPIO.cpp` 纯可选（把 VSYNC/FRAME_RATE 事件打到 GPIO 上给示波器看）。

**USER CODE 保留区精确行号（2026-09-25 实测）**
| 文件 | 总行 | 保留区 | 三个必修函数体是否在区内 |
|---|---|---|---|
| `TouchGFXHAL.cpp` | 163 | L25 – L161 | ✅ `flushFrameBuffer` 在 L77-90（区内） |
| `TouchGFXHAL.hpp` | 169 | L25 – L165 | 加成员变量/私有 helper 写这里 |
| `STM32TouchController.cpp` | 52 | L23 – L50 | ✅ `init` L27-33、`sampleTouch` L35-48 |
| `STM32TouchController.hpp` | 72 | L23 – L70 | |
| `TouchGFXGPIO.cpp` | 176 | L39 – L174 | 可选 |

头尾的 `USER CODE BEGIN/END Header`（L1/L21）和 `COPYRIGHT ... END OF FILE` 不要动。

### ⚠️ `advanceFrameBufferToRect` 不能跨文件调用（实测）
它在 `TouchGFXGeneratedHAL.hpp:174` 声明为 `inline`、定义在 `TouchGFXGeneratedHAL.cpp:72`，而该 .cpp 内**没有任何调用**→ GCC 直接丢弃了这个 inline 定义。实测 `nm build/Debug/spi_demo.elf | grep advanceFrameBuffer` **无匹配**。
在 `TouchGFXHAL.cpp` 里调它会 **undefined reference**（TouchGFXGeneratedHAL.cpp.obj 不会被重编，符号补不回来）。
→ **自己在 `flushFrameBuffer` 里算指针**：
```cpp
/* ⚠️ 不要用 getDisplayWidth()*2！旋转生效后它返回 320 → 640，是错的 */
const uint16_t stride = lcd().framebufferStride();   /* = FRAME_BUFFER_WIDTH*2 = 480 */
uint8_t* p = (uint8_t*)getTFTFrameBuffer()
           + (uint32_t)rect.y * stride + (uint32_t)rect.x * 2;
```
接口名注意：HAL **没有** `getWidth()`，是 `getDisplayWidth()` / `getDisplayHeight()`（`HAL.hpp:624/634`，public）——**但这两个受旋转影响，不能当行距用**。
行距（字节）必须用 `lcd().framebufferStride()`（`LCD16bpp.hpp:83` → `FRAME_BUFFER_WIDTH * 2`，**不随旋转变**）。
`getTFTFrameBuffer()` 是 **protected**（但派生类成员内可调 ✅）。
`Rect` 的 `x/y/width/height` 都是 **public** 成员（`touchgfx/hal/Types.hpp:123`）。
`HAL::flushFrameBuffer(rect)` 必须**最后**调（通知框架已刷完）。

### 触摸的前置条件（尚未具备）
`STM32TouchController` 要读电容触摸 IC（CTC2.8），但 `spi_demo.ioc` 里 **I2C 未启用**、也没配触摸中断脚。
→ 写 `init()`/`sampleTouch()` 之前需要先在 CubeMX 里加 I2C 并给出清单（按用户规矩由用户自己改）。

## TouchGFX 集成现状（2026-09-24）
- 编译通过：ROM 1,003,736 B / 2 MB（47.86%）、RAM 157,752 B / 768 KB（20.06%）
- 框架走 **ST 预编译库** `Middlewares/ST/touchgfx/lib/core/cortex_m33/gcc/libtouchgfx-float-abi-hard.a`，不编译 97 个框架 .cpp
- `USE_BPP=16` 由 `cmake/touchgfx/CMakeLists.txt` 定义
- 未完成（不显示的原因）：① `OSWrappers::signalVSync()` 全工程无调用者 → 框架不推进；② `TouchGFXHAL::flushFrameBuffer()` 为空；③ `STM32TouchController` 空壳
- 内存可省 750 KB：`yuexinmao1~4`（614,400 B，已无用）+ `frameBuf` 被放成 LOAD 段占 153,600 B Flash（`.ld` 里缺 `TouchGFX_Framebuffer` 段，需加 `(NOLOAD)`）

## `Middlewares/ST/touchgfx/` 与 `TouchGFX/` 的关系（2026-09-27 核实）

**一句话：Middleware = 库（ST 提供，别动）；TouchGFX = 本工程（你的东西）。单向依赖 TouchGFX → Middlewares。**

### Middlewares 是"裁剪过的拷贝"，源在 `C:\TouchGFX\4.26.1\touchgfx\`
逐目录文件数对比（工程 vs 安装）：`framework/include` 321=321、`framework/source` 97=97、`framework/tools` 75=75、`os` 2=2、`3rdparty` 17=17 **全等**；**只有 `lib` 是 38 vs 95**（只拷本工程需要的编译器/平台变体）。
→ Designer 执行 Generate Code 时从本机 TouchGFX 安装目录**整体拷贝**，仅裁剪 `lib/`。

### Middlewares 内部构成（550 文件 / 117 MB）
- `framework/include/` 321 文件 —— 唯一被 `-I` 的部分（`<touchgfx/hal/HAL.hpp>` 等）
- `framework/source/` 97 个 .cpp —— **完全不编译**（改用 `lib/` 的预编译 .a；同份代码的两种形态）
- `framework/tools/` 75 文件 —— 4 个 PC 转换器：`fontconvert` / `imageconvert` / `textconvert` / `videoconvert`（Designer 调它们把 PNG/TTF 转 C++）
- `lib/` 38 文件 —— `core/cortex_m33/{gcc,IAR8.x,IAR9.x,Keil6.x,stclang}` + `linux/` + `sdl2/win32` + `win/{mingw32,msvs}`。**后三者是 PC 模拟器用的**，工程只用 `core/cortex_m33/gcc/libtouchgfx-float-abi-hard.a`
- `os/` 2 文件（`OSWrappers.cpp` / `OSWrappers_cmsis.cpp`）、`3rdparty/libjpeg/` 17 文件

### 编译接线（两条并行，各管一层）
`cmake/touchgfx/CMakeLists.txt`（**Designer 生成**）：
- L11 **唯一一条**指向 Middlewares 的 include：`Middlewares/ST/touchgfx/framework/include`
- L72 **唯一一个**来自 Middlewares 的库：`lib/core/cortex_m33/gcc/libtouchgfx${HARD_FLOAT}.a`
- L22-51 共 28 个源文件全部来自 `TouchGFX/gui/` + `TouchGFX/generated/`

`cmake/stm32cubemx/CMakeLists.txt`（**CubeMX 生成**）：
- L14-16 include `TouchGFX/App`、`TouchGFX/target/generated`、`TouchGFX/target`
- L25-32 编译 `app_touchgfx.c` + `target/generated/*.cpp` + `target/*.cpp`（含你的 3 个文件）

→ **`target/` 层归 CubeMX 管，`gui/` + `generated/` 层归 Designer 管**，框架只提供头文件 + 一个 .a。L72 那个空的 `# Drivers Midllewares` 与 TouchGFX 无关，不用管。
⚠️ 别手动删 `Middlewares/`（Designer 会重拷，但删了没重新生成就直接编不过）。

### 另注（2026-09-27）
`.ioc` 的 `Mcu.IP` 仍无 I2C（只有 RCC 时钟树里出现 `I2C1Freq_Value` 字样），`Core/Src/` 也没有 `i2c.c`。
但 `cmake/stm32cubemx/CMakeLists.txt:52-53` 却列了 `stm32u5xx_hal_i2c.c/_ex.c` —— HAL 源文件列表与 .ioc 不完全同步，属正常，不改变「I2C 未启用」的结论。

## ★ LCD 排针引脚映射（2026-09-27 从原理图读出，重要）

来源 `FS-STM32U5-v2.0核心板原理图(2023.07.29).pdf`（1 页；排针在右上 x≈640-800/y≈415-500，**文字竖排，必须渲染成图才能读准**）。

| 脚 | 网络 | 备注 |
|---|---|---|
| 1–3 | VCC-3V3 | C18 10uF/16V、C19 0.1uF/50V 去耦 |
| 4–6 | GND | |
| 7 | PA4 = SPI1_NSS | 硬件 NSS 脚，**工程当 DCX 用**（`main.h:60`） |
| 8 | PA5 = SPI1_SCK | |
| 9 | PA6 = SPI1_MISO | |
| 10 | PA7 = SPI1_MOSI | |
| 11 | PA8 = LCD_RST | `main.h:62` |
| 12 | **PB5 = INT-TP** | 触摸中断 |
| 13 | **PB6 = I2C1_SCL** | 触摸 I2C，10k 上拉（R19/R20） |
| 14 | **PB7 = I2C1_SDA** | 同上 |
| 15 | **PB10 = LCD_BL** | 背光控制 |
| 16 | **PA11 = LCD_TE** | 撕裂输出 |
| 17 | 空脚 | |
| 18–20 | NC | 图上打红叉 |
| 21–22 | GND | |

### ⚠️ 更正：TE 与触摸的硬件接线是存在的
旧笔记多处写过「TE 引脚没接出来，`main.h` 只定义了 DCX(PA4)/RST(PA8)」——**错**。
**TE 已接到 PA11，触摸 I2C1 在 PB6/PB7、中断在 PB5，全部有硬件通路**，只是 `.ioc` 里一条配置都没有（`PA11/PB5/PB6/PB7/PB10` 实测均为 0 条）。
→ **做 TE 同步和触摸都不需要飞线**，CubeMX 里加外设即可。给用户的清单：
① `Connectivity → I2C1` 启用，PB6=SCL/PB7=SDA（AF 开漏+上拉）；② PB5 → GPIO_EXTI（触摸中断）；③ PA11 → GPIO_EXTI（TE 同步）；④ 可选 PB10 → GPIO_Output（背光）。

## ★★ 分辨率/方向错配：Designer 是 320×240 横屏，实物屏是 240×320 竖屏（2026-09-27 发现）

三处配置互相打架：

| 位置 | 值 | 说明 |
|---|---|---|
| `TouchGFX/target/generated/TouchGFXConfiguration.cpp:36` | `TouchGFXHAL hal(dma, display, tc, **240, 320**)` | HAL 构造 → `nativeDisplayOrientation = PORTRAIT` |
| `TouchGFX/generated/gui_generated/src/common/FrontendApplicationBase.cpp` | `setDisplayOrientation(**ORIENTATION_LANDSCAPE**)` | 请求横屏 |
| `TouchGFX/generated/gui_generated/src/screen_screen/screenViewBase.cpp` | `__background.setPosition(0, 0, **320, 240**)`；`button1.setXY(**-25**, 70)` | Designer 画布就是 320×240 横屏，按钮还是负坐标（一半在画布外） |

**运行时会发生什么**：`setDisplayOrientation` 只是记下请求（`HAL.hpp:131-135`），真正生效在 `performDisplayOrientationChange()`（`HAL.hpp:1371-1389`）：
`requested(LANDSCAPE) != native(PORTRAIT)` 且 `DISPLAY_ROTATION == rotate0` → **交换 `DISPLAY_WIDTH`↔`DISPLAY_HEIGHT`（变成 320×240），并置 `DISPLAY_ROTATION = rotate90`**。
⚠️ 注意 **`FRAME_BUFFER_WIDTH` 不参与交换**，仍是 240。

### 由此得出的两条硬结论
1. **行距必须用 `lcd().framebufferStride()`（=480），不能用 `getDisplayWidth()*2`**——旋转后 `getDisplayWidth()` 返回 320，会算出 640（见上一节的更正）。
2. Designer 画布与实物屏方向不一致，**要么把 Designer 工程改成 240×320 竖屏**（`FrontendApplicationBase` 里那句 `setDisplayOrientation` 是 Designer 生成的，改 Designer 的屏设置即可），**要么接受横幅 UI 并把实物屏横过来看**。另外 `button1.setXY(-25, 70)` 这半个出界的按钮要修。

## `gui/` MVP 骨架（10 文件，全是空壳，2026-09-27）

| 层 | 文件 | 现状 |
|---|---|---|
| Model | `gui/{include,src}/model/Model.hpp/.cpp` | 只有构造 + 空 `tick()` + `bind(ModelListener*)` |
| ModelListener | `model/ModelListener.hpp` | 只有 `bind(Model*)`，无虚函数 |
| View | `screen_screen/screenView.hpp/.cpp` | `setupScreen()` / `tearDownScreen()` 只转发基类 |
| Presenter | `screen_screen/screenPresenter.hpp/.cpp` | 空 `activate()` / `deactivate()`；继承 `touchgfx::Presenter` + `ModelListener` |
| Application | `common/FrontendApplication.hpp/.cpp` | `handleTickEvent()` **内联定义在 .hpp 里**（要加 tick 逻辑改头文件）→ 调 `model.tick()` |
| Heap | `common/FrontendHeap.hpp` | 单例 + 静态 `Partition<...,1>`；`UserDefinedViewTypes/PresenterTypes/TransitionTypes` 三处目前都是 `meta::Nil`（加用户自定义类要登记）；`Model model; FrontendApplication app;` 是成员，构造里 `gotoStartScreen(app)` |

继承链：`screenView : screenViewBase`（基类在 `generated/gui_generated/`，含控件成员与 `add()`）→ `FrontendApplication : FrontendApplicationBase : touchgfx::MVPApplication`。
每 tick 链：`MX_TouchGFX_Process()` → `touchgfx_taskEntry()` → `hal.backPorchExited()` → `FrontendApplication::handleTickEvent()` → `Model::tick()`。
