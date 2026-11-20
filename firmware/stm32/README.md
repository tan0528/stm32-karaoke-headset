# STM32 侧对接层

这个目录里的代码**不参与 PC 构建**（`make` 只编 `firmware/audio_core.c`），
它是给目标板用的参考实现。之所以放进仓库，是因为"核心逻辑与硬件解耦"
这件事，光有核心文件看不出来——得看到同一份 `audio_core.c` 是怎么被
HAL 回调驱动的。

## 文件

| 文件 | 作用 |
|---|---|
| `board_pins.h` | 引脚与通道分配，移植时只改这里 |
| `app_audio.c/h` | ADC+DMA 半/全缓冲回调、8kHz 时基、启动顺序 |
| `app_control.c/h` | 按键 → 状态 → 执行器，1ms 节拍 |
| `drv_mcp41010.c/h` | 软件 SPI 驱动数字电位器 |
| `drv_cd4066.c/h` | 模拟开关：静音 / 输出选择 / K 歌使能 |
| `main.c` | 初始化与主循环示例 |

## 移植到自己的工程

1. CubeMX 里按 `board_pins.h` 配好引脚，生成工程。
2. 把 `firmware/audio_core.c` 加进工程（和 PC 上编的是同一个文件）。
3. 把本目录的 `app_*.c`、`drv_*.c` 加进工程。
4. 确认 CubeMX 的 ADC 配置：

   ```
   Resolution            = 12 bit
   Continuous Conversion = Disable     ← 必须关，否则采样率不再受 TIM3 控制
   External Trigger      = TIM3_TRGO
   Trigger Edge          = Rising
   Data Alignment        = Right
   DMA                   = Circular, Peripheral→Memory, Half Word,
                           Memory Increment = Enable
   ```

5. 确认 TIM3：`PSC = 71, ARR = 124`（72MHz / 72 / 125 = 8kHz），
   `Trigger Event Selection = Update Event`。
6. 确认 TIM2：`PSC = 0, ARR = 719`（100kHz），PWM 模式 1。

## 启动顺序不能换

```c
HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);                 /* 1. 载波先稳 */
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)buf, 256);           /* 2. 挂上，等触发 */
HAL_TIM_Base_Start_IT(&htim3);                            /* 3. 8kHz 开始 */
```

倒过来会出现"定时器已经触发、ADC/DMA 还没准备好"的空窗，
表现为开机头 16ms 的爆音。

## 一个绕不开的设计缺陷

原理图上麦克风前置放大输出与耳机线控分压**共用 ADC1_IN0**：
K 歌支路靠 U1-C 把 MIC 接到 PA0，而耳机座的 M 脚也落在 PA0。

K 歌模式下 PA0 的电位由前置放大输出（静态 1.65V ≈ 2048 counts）主导，
正好落进"音量+"的判决区间，于是线控会被判成一直按住音量加。

`app_control_tick_1ms()` 的处理是：K 歌模式下直接暂停线控扫描。
这是妥协，不是修复。真要做对，得把线控挪到独立 ADC 通道，
或者用分时开关在两次采样之间切换。

## 这个目录没有编译验证

它依赖 `stm32f1xx_hal.h`，仓库里不打包 HAL。CI 只检查
`firmware/audio_core.c` 与 `sim/`、`tests/` —— 也就是能被 PC 编译的那部分。
本目录的代码是按 STM32F1 HAL 的接口写的，实际移植时仍以你工程的
CubeMX 版本为准。
