/*
 * app_audio.c -- 音频通路：HAL 对接层
 *
 * 数据流：
 *
 *   TIM3 (8kHz TRGO) --> ADC1 --> DMA(循环, 256 点)
 *                                   |-- 半缓冲完成 --> audio_block_process --> 队列
 *    TIM3 (8kHz 中断) --------------------------> 从队列取一个 CCR --> TIM2->CCR1
 *
 * PWM 载波固定 100kHz 一直跑，只有 CCR 按 8kHz 换。
 * 把这两个频率搞混是这块最典型的错误：以为"每秒更新 10 万个样本"，
 * 实际播放速度会快 12.5 倍。
 */
#include "app_audio.h"

#include <string.h>

#include "audio_core.h"
#include "board_pins.h"
#include "drv_cd4066.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;

#define QDEPTH        4
#define HALF_LEN_SLOT 2u

static uint16_t s_adc_buf[AUDIO_BLOCK_LEN];           /* DMA 目标 */
static uint16_t s_ccr[HALF_LEN_SLOT][AUDIO_HALF_LEN]; /* 两块交替 */
static uint16_t s_q[QDEPTH][AUDIO_HALF_LEN];

static volatile uint8_t  s_q_head;
static volatile uint8_t  s_q_tail;
static volatile uint8_t  s_q_count;
static volatile uint16_t s_q_idx;

static volatile uint16_t s_gain = 256u;
static volatile uint8_t  s_karaoke = 0u;
static volatile uint8_t  s_running = 0u;

static app_audio_stats_t s_stats;

/* ---------------------------------------------------------------
 * 块处理：在中断里跑，必须短
 * --------------------------------------------------------------- */
static void process_half(const uint16_t *src, uint16_t *dst)
{
    audio_block_process(src, dst, AUDIO_HALF_LEN, s_gain);

    if (s_q_count < QDEPTH) {
        memcpy(s_q[s_q_head], dst, sizeof(s_q[s_q_head]));
        s_q_head = (uint8_t)((s_q_head + 1u) % QDEPTH);
        s_q_count++;
    } else {
        s_stats.block_overrun++;
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    app_audio_on_adc_half(hadc);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    app_audio_on_adc_cplt(hadc);
}

void app_audio_on_adc_half(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) { return; }
    process_half(&s_adc_buf[0], s_ccr[0]);
}

void app_audio_on_adc_cplt(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) { return; }
    process_half(&s_adc_buf[AUDIO_HALF_LEN], s_ccr[1]);
}

/* ---------------------------------------------------------------
 * 8kHz 时基：取一个占空比给 PWM
 * --------------------------------------------------------------- */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    app_audio_on_tim3_tick(htim);
}

void app_audio_on_tim3_tick(TIM_HandleTypeDef *htim)
{
    uint16_t v;

    if (htim->Instance != TIM3) { return; }

    if (s_q_count == 0u) {
        /* 上游还没填好：维持中点，等于静音而不是随机噪声 */
        s_stats.fifo_underrun++;
        v = (uint16_t)(PWM_PERIOD / 2u);
    } else {
        v = s_q[s_q_tail][s_q_idx];
        if (++s_q_idx >= AUDIO_HALF_LEN) {
            s_q_idx = 0u;
            s_q_tail = (uint8_t)((s_q_tail + 1u) % QDEPTH);
            s_q_count--;
        }
    }

    /* 直接写寄存器：HAL_TIM_PWM 的调用开销在 8kHz 中断里不值得 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, v);
}

/* ---------------------------------------------------------------
 * 初始化与启停
 * --------------------------------------------------------------- */
void app_audio_init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_q_head = 0u;
    s_q_tail = 0u;
    s_q_count = 0u;
    s_q_idx = 0u;
    s_gain = 256u;
    s_karaoke = 0u;
    s_running = 0u;
}

void app_audio_start(void)
{
    if (s_running) { return; }

    /* 1. 先开 100kHz 载波，让它稳定在 50% 占空比 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, PWM_PERIOD / 2u);
    (void)HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

    /* 2. 再挂 ADC + DMA，此时它只是等触发，还不会产生数据 */
    (void)HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_buf, AUDIO_BLOCK_LEN);

    /* 3. 最后开时基，8kHz 从此开始真正采样 */
    (void)HAL_TIM_Base_Start_IT(&htim3);

    s_running = 1u;
}

void app_audio_stop(void)
{
    if (!s_running) { return; }

    (void)HAL_TIM_Base_Stop_IT(&htim3);
    (void)HAL_ADC_Stop_DMA(&hadc1);
    (void)HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);

    s_running = 0u;
}

void app_audio_set_gain(uint16_t q8_gain)
{
    s_gain = q8_gain;
}

uint16_t app_audio_get_gain(void)
{
    return s_gain;
}

void app_audio_set_karaoke(int on)
{
    s_karaoke = on ? 1u : 0u;
    cd4066_set_mic_path(on);
}

int app_audio_karaoke_enabled(void)
{
    return (int)s_karaoke;
}

int app_audio_adc_busy_with_mic(void)
{
    /* K 歌模式下 PA0 被前置放大输出占用，线控分压读不到真实值 */
    return (int)s_karaoke;
}

void app_audio_get_stats(app_audio_stats_t *out)
{
    if (out == NULL) { return; }
    out->block_overrun  = s_stats.block_overrun;
    out->fifo_underrun  = s_stats.fifo_underrun;
}
