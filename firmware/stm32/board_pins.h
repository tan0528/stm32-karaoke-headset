/*
 * board_pins.h -- 引脚分配（对应原理图）
 *
 * ⚠ 这些宏来自原理图反推，不是 CubeMX 工程里导出的。
 *   移植到自己板子上时，只需改这个文件 + 重新生成 CubeMX 配置。
 *
 * 名称              引脚    方向    作用
 * ---------------------------------------------------------------
 * 麦克风采样        PA0     模拟    ADC1_IN0，经 U1-C 使能后接入前置放大输出
 * 线控按键          PA0     模拟    ADC1_IN0，耳机座 M 脚经 R14=1k 下拉后分压
 * 主通路使能        PA1     输出    U1 的 A/B 两组开关：L->LI、R->RI（同时静音两声道）
 * 扬声器汇总        PA2     输出    U2 的 C/D 两组：L2、R2 汇成 RL 单声道送功放
 * 耳机输出          PA3     输出    U2 的 A/B 两组：L2->L3、R2->R3
 * 数字电位器 CS     PA4     输出    U5、U6 两片并联，一帧同时写入
 * 数字电位器 SCK    PA5     输出    GPIO 模拟 SPI
 * 数字电位器 SI     PA6     输出    GPIO 模拟 SPI（SPI1 的 MOSI 在 PA7，故用软件时序）
 * K 歌支路使能      PB10    输出    U1 的 C 组：把 MIC 接到 PA0
 * 本机按键          PA7/PB1/PB0/PB15  输入   模式 / 静音 / 音量- / 音量+
 * OLED              PB6/PB7 I2C1    SCL / SDA，需 4.7k 上拉
 * PWM 输出          PA?     复用    TIM2_CH1，100kHz 载波
 * ADC 触发          -       -       TIM3 TRGO，8kHz
 *
 * 已知设计问题（见 docs/architecture.md 第 5 节）：
 * 麦克风前置输出与线控分压共用 ADC1_IN0 一个通道。K 歌模式下
 * U1-C 导通，PA0 上的电位由前置放大（静态 1.65V，折合 2048 counts）
 * 主导，正好落进"音量+"的判决区间，会把 K 歌误判成连续按键。
 * 因此固件在 K 歌模式下暂停线控扫描 —— 这是设计妥协，不是 bug 修复。
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32f1xx_hal.h"

/* ---- 数字电位器（软件 SPI） ---- */
#define DPOT_CS_PORT      GPIOA
#define DPOT_CS_PIN       GPIO_PIN_4
#define DPOT_SCK_PORT     GPIOA
#define DPOT_SCK_PIN      GPIO_PIN_5
#define DPOT_SI_PORT      GPIOA
#define DPOT_SI_PIN       GPIO_PIN_6

/* ---- CD4066 模拟开关 ---- */
#define SW_MAIN_PORT      GPIOA     /* U1 A/B：主通路通断（一键静音） */
#define SW_MAIN_PIN       GPIO_PIN_1
#define SW_MIC_PORT       GPIOB     /* U1 C：K 歌支路使能 */
#define SW_MIC_PIN        GPIO_PIN_10
#define SW_SPK_PORT       GPIOA     /* U2 C/D：扬声器（单声道汇总） */
#define SW_SPK_PIN        GPIO_PIN_2
#define SW_HP_PORT        GPIOA     /* U2 A/B：耳机 */
#define SW_HP_PIN         GPIO_PIN_3

/* ---- 本机按键 ---- */
#define KEY_MODE_PORT     GPIOA
#define KEY_MODE_PIN      GPIO_PIN_7
#define KEY_MUTE_PORT     GPIOB
#define KEY_MUTE_PIN      GPIO_PIN_1
#define KEY_VOLD_PORT     GPIOB
#define KEY_VOLD_PIN      GPIO_PIN_0
#define KEY_VOLU_PORT     GPIOB
#define KEY_VOLU_PIN      GPIO_PIN_15

/* ---- 线控按键 ADC 通道 ---- */
#define KEY_ADC_CHANNEL   ADC_CHANNEL_0

/* ---- 采样与定时参数 ---- */
#define AUDIO_ADC_FS      8000u    /* TIM3 8kHz 触发 */
#define AUDIO_PWM_FS      100000u  /* TIM2 100kHz 载波 */
#define AUDIO_BLOCK_LEN   256u     /* DMA 循环缓冲总长，半缓冲 128 */
#define AUDIO_HALF_LEN    (AUDIO_BLOCK_LEN / 2u)

#endif /* BOARD_PINS_H */
