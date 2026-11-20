/*
 * app_audio.h -- 音频通路的启动、中断回调与增益控制
 *
 * 这一层负责把 audio_core（纯逻辑）接到 STM32 的 HAL 上：
 *   TIM3 更新事件（TRGO） -> ADC 采样 -> DMA 半/全缓冲中断 -> audio_core
 *   TIM3 更新事件（中断） -> 从已完成队列取一个占空比写到 TIM2->CCR1
 *
 * 注意 TIM3 的一次更新事件同时做两件事：触发 ADC，以及更新 PWM 占空比。
 * 前者走硬件（TRGO），后者走中断，两者节拍一致但路径不同。
 */
#ifndef APP_AUDIO_H
#define APP_AUDIO_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

void app_audio_init(void);

/* 启动顺序很重要：先开载波，再挂 DMA，最后开 8kHz 时基。
 * 反过来会出现"定时器已经在触发，ADC/DMA 还没准备好"的空转。 */
void app_audio_start(void);
void app_audio_stop(void);

/* 由 HAL_ADC_ConvHalfCpltCallback / ConvCpltCallback 转发 */
void app_audio_on_adc_half(ADC_HandleTypeDef *hadc);
void app_audio_on_adc_cplt(ADC_HandleTypeDef *hadc);

/* 由 HAL_TIM_PeriodElapsedCallback（TIM3）转发，8kHz */
void app_audio_on_tim3_tick(TIM_HandleTypeDef *htim);

/* 麦克风软件增益，Q8 定点，256 = 1.0x */
void     app_audio_set_gain(uint16_t q8_gain);
uint16_t app_audio_get_gain(void);

/* K 歌支路开关：同时控制 CD4066 与 ADC 采集 */
void app_audio_set_karaoke(int on);
int  app_audio_karaoke_enabled(void);

/* 供 app_control 判断线控 ADC 是否可用 */
int  app_audio_adc_busy_with_mic(void);

/* 运行统计，便于调试打印 */
typedef struct {
    uint32_t block_overrun;    /* CPU 没跟上、整块被丢弃的次数 */
    uint32_t fifo_underrun;    /* 消费端取不到数据的 tick 数 */
} app_audio_stats_t;

void app_audio_get_stats(app_audio_stats_t *out);

#endif /* APP_AUDIO_H */
