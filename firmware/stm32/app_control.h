/*
 * app_control.h -- 人机交互：按键、音量、静音、输出选择
 *
 * 两套完全独立的输入：
 *   1. 耳机线控  —— 经 ADC 读 MIC 线上的分压，模拟量，走 key_fsm 消抖
 *   2. 板上四键  —— 普通 GPIO 按键，模式 / 静音 / 音量 - / 音量 +
 *
 * 状态变迁最终只落到三个执行器上：
 *   MCP41010（音量）、CD4066 主通路（静音）、CD4066 输出选择（耳机/扬声器）
 */
#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdint.h>

#include "drv_cd4066.h"

typedef struct {
    uint8_t      volume;     /* 0..100 */
    uint8_t      muted;      /* 1 = 主通路切断 */
    uint8_t      karaoke;    /* 1 = 麦克风支路使能 */
    output_sel_t output;
} app_state_t;

/* 显示回调：状态变化时才调用一次，不要在主循环里刷屏。
 * 传 NULL 表示不接显示（无 OLED 的板子）。 */
typedef void (*app_display_cb_t)(const app_state_t *st);

void app_control_init(void);
void app_control_set_display_cb(app_display_cb_t cb);

/* 每 1ms 调用一次（建议放在 SysTick 或 TIM 中断里）。 */
void app_control_tick_1ms(void);

const app_state_t *app_control_state(void);

/* 便于测试或串口命令直接设定 */
void app_control_set_volume(uint8_t volume);
void app_control_toggle_mute(void);
void app_control_toggle_karaoke(void);

#endif /* APP_CONTROL_H */
