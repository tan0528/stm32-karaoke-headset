/*
 * app_control.c -- 按键 -> 状态 -> 执行器
 *
 * 设计上把"状态"和"执行器"分开：所有改动都先落在 app_state_t，
 * 再由 apply() 统一推到 GPIO / 数字电位器。这样任何一个动作
 * 都不会只改了一半（比如音量变了但显示没变）。
 */
#include "app_control.h"

#include <stddef.h>

#include "app_audio.h"
#include "audio_core.h"
#include "board_pins.h"
#include "drv_mcp41010.h"

/* 板载按键按下时为低电平（按键另一端接 GND） */
#define LOCAL_KEY_PRESSED(port, pin) \
    (HAL_GPIO_ReadPin((port), (pin)) == GPIO_PIN_RESET)

#define LOCAL_KEY_DEBOUNCE_MS 20u

static app_state_t      s_state;
static app_display_cb_t s_display;
static uint8_t          s_dirty;

/* 线控：1ms 一次的采样与消抖 */
static key_fsm_t s_key_fsm;
static uint16_t  s_key_adc;

extern ADC_HandleTypeDef hadc1;

/* ---------------------------------------------------------------
 * 板载按键：20ms 边沿消抖
 * --------------------------------------------------------------- */
typedef struct {
    uint8_t cnt;
    uint8_t stable;    /* 1 = 已按下 */
} btn_t;

static btn_t s_btn_mode, s_btn_mute, s_btn_vold, s_btn_volu;

/* 返回 1 表示本次 tick 刚刚发生"按下"沿 */
static int btn_scan(btn_t *b, GPIO_TypeDef *port, uint16_t pin)
{
    uint8_t raw = (uint8_t)(LOCAL_KEY_PRESSED(port, pin) ? 1u : 0u);

    if (raw != b->stable) {
        if (b->cnt < LOCAL_KEY_DEBOUNCE_MS) {
            b->cnt++;
        } else {
            b->stable = raw;
            b->cnt = 0u;
            if (raw != 0u) {
                return 1;      /* 只在按下时触发，松开不重复触发 */
            }
        }
    } else {
        b->cnt = 0u;
    }
    return 0;
}

/* ---------------------------------------------------------------
 * 执行器
 * --------------------------------------------------------------- */
static void apply(void)
{
    cd4066_set_main_pass(s_state.muted ? 0 : 1);
    cd4066_set_output(s_state.output);
    app_audio_set_karaoke(s_state.karaoke);

    /* 音量与静音是两件独立的事：静音不动数字电位器，
     * 取消静音后音量还是原来的值 */
    mcp41010_set_level(s_state.volume);

    s_dirty = 1u;
}

static void flush_display(void)
{
    if (s_dirty == 0u) { return; }
    s_dirty = 0u;
    if (s_display != NULL) {
        s_display(&s_state);
    }
}

/* ---------------------------------------------------------------
 * 对外接口
 * --------------------------------------------------------------- */
void app_control_init(void)
{
    s_state.volume  = 50u;
    s_state.muted   = 0u;
    s_state.karaoke = 0u;
    s_state.output  = OUTPUT_HEADPHONE;

    s_display = NULL;
    s_dirty = 0u;

    s_btn_mode.cnt = s_btn_mute.cnt = s_btn_vold.cnt = s_btn_volu.cnt = 0u;
    s_btn_mode.stable = s_btn_mute.stable = s_btn_vold.stable = s_btn_volu.stable = 0u;

    key_fsm_init(&s_key_fsm);
    s_key_adc = 4095u;

    apply();
}

void app_control_set_display_cb(app_display_cb_t cb)
{
    s_display = cb;
    s_dirty = 1u;
}

void app_control_set_volume(uint8_t volume)
{
    if (volume > 100u) { volume = 100u; }
    if (s_state.volume == volume) { return; }
    s_state.volume = volume;
    apply();
}

void app_control_toggle_mute(void)
{
    s_state.muted = (uint8_t)(s_state.muted ? 0u : 1u);
    apply();
}

void app_control_toggle_karaoke(void)
{
    s_state.karaoke = (uint8_t)(s_state.karaoke ? 0u : 1u);
    apply();
}

const app_state_t *app_control_state(void)
{
    return &s_state;
}

/* ---------------------------------------------------------------
 * 1ms 节拍
 * --------------------------------------------------------------- */
void app_control_tick_1ms(void)
{
    key_id_t ev;

    /* ---- 1. 板载四键 ---- */
    if (btn_scan(&s_btn_mode, KEY_MODE_PORT, KEY_MODE_PIN)) {
        app_control_toggle_karaoke();
    }
    if (btn_scan(&s_btn_mute, KEY_MUTE_PORT, KEY_MUTE_PIN)) {
        app_control_toggle_mute();
    }
    if (btn_scan(&s_btn_vold, KEY_VOLD_PORT, KEY_VOLD_PIN)) {
        if (s_state.volume >= 5u) { app_control_set_volume((uint8_t)(s_state.volume - 5u)); }
        else                      { app_control_set_volume(0u); }
    }
    if (btn_scan(&s_btn_volu, KEY_VOLU_PORT, KEY_VOLU_PIN)) {
        if (s_state.volume <= 95u) { app_control_set_volume((uint8_t)(s_state.volume + 5u)); }
        else                       { app_control_set_volume(100u); }
    }

    /* ---- 2. 耳机线控 ---- */
    /* K 歌模式下 PA0 被前置放大输出占用，此时读到的不是按键分压，
     * 判决会落进"音量+"区间，表现为音量自己往上跑。直接暂停扫描。 */
    if (app_audio_adc_busy_with_mic()) {
        s_key_adc = 4095u;
        key_fsm_init(&s_key_fsm);
    } else {
        (void)HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 2u) == HAL_OK) {
            s_key_adc = (uint16_t)HAL_ADC_GetValue(&hadc1);
        }
    }

    ev = key_fsm_update(&s_key_fsm, s_key_adc);
    switch (ev) {
        case KEY_PLAY:
            app_control_toggle_mute();
            break;
        case KEY_VOLUP:
            if (s_state.volume <= 95u) { app_control_set_volume((uint8_t)(s_state.volume + 5u)); }
            else                       { app_control_set_volume(100u); }
            break;
        case KEY_VOLDOWN:
            if (s_state.volume >= 5u) { app_control_set_volume((uint8_t)(s_state.volume - 5u)); }
            else                      { app_control_set_volume(0u); }
            break;
        default:
            break;
    }

    flush_display();
}
