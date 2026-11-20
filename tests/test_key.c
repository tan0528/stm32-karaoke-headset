/*
 * test_key.c -- 线控按键：阈值判决 + 消抖状态机
 *
 * 状态机的语义是"按下沿事件"，所以测试关心的不是电平，而是：
 *   1. 按住不放只能产生一次事件（否则长按 = 连击）；
 *   2. 一次毛刺不能产生事件；
 *   3. 松开后能重新触发。
 */
#include "audio_core.h"
#include "test_util.h"

#define ADC_NONE  4095u
#define ADC_PLAY  3000u
#define ADC_VOLU  2000u
#define ADC_VOLD   800u

static void t_thresholds(void)
{
    CHECK(key_raw_decode(4095u) == KEY_NONE);
    CHECK(key_raw_decode(3601u) == KEY_NONE);
    CHECK(key_raw_decode(3600u) == KEY_PLAY);    /* 边界归上区间 */
    CHECK(key_raw_decode(2601u) == KEY_PLAY);
    CHECK(key_raw_decode(2600u) == KEY_VOLUP);
    CHECK(key_raw_decode(1401u) == KEY_VOLUP);
    CHECK(key_raw_decode(1400u) == KEY_VOLDOWN);
    CHECK(key_raw_decode( 401u) == KEY_VOLDOWN);
    CHECK(key_raw_decode( 400u) == KEY_NONE);    /* 低于下限视为异常 */
    CHECK(key_raw_decode(   0u) == KEY_NONE);
}

/* 连续喂同一个 ADC 值，返回事件次数，并记录首次事件发生在第几个 tick */
static int feed(key_fsm_t *k, uint16_t adc, int ticks, int *first_tick)
{
    int events = 0;
    int i;

    for (i = 1; i <= ticks; i++) {
        if (key_fsm_update(k, adc) != KEY_NONE) {
            events++;
            if (events == 1 && first_tick != NULL) { *first_tick = i; }
        }
    }
    return events;
}

static void t_single_event_on_press(void)
{
    key_fsm_t k;
    int first = -1;
    int events;

    key_fsm_init(&k);
    events = feed(&k, ADC_PLAY, 60, &first);

    CHECK(events == 1);
    /* 5 次一致确认 + 1 次上报，所以第 7 个 tick 出事件 */
    CHECK_MSG(first == 7, "first_tick=%d", first);
}

static void t_hold_no_repeat(void)
{
    key_fsm_t k;
    int events;

    key_fsm_init(&k);
    events = feed(&k, ADC_PLAY, 500, NULL);
    CHECK_MSG(events == 1, "长按 500ms 产生了 %d 次事件", events);
}

static void t_release_allows_retrigger(void)
{
    key_fsm_t k;
    int events;

    key_fsm_init(&k);
    events = feed(&k, ADC_PLAY, 20, NULL);
    events += feed(&k, ADC_NONE, 20, NULL);      /* 松开，不应有事件 */
    CHECK_MSG(events == 1, "松开时产生了多余事件，共 %d", events);
    events += feed(&k, ADC_PLAY, 20, NULL);
    CHECK_MSG(events == 2, "第二次按下未被识别，共 %d", events);
}

static void t_glitch_rejected(void)
{
    key_fsm_t k;
    int events;

    key_fsm_init(&k);
    events  = feed(&k, ADC_PLAY, 10, NULL);
    events += feed(&k, ADC_NONE, 1, NULL);       /* 1ms 毛刺 */
    events += feed(&k, ADC_PLAY, 30, NULL);

    /* 毛刺把消抖计数器清零，但不会产生额外的按下沿 */
    CHECK_MSG(events == 1, "毛刺产生了额外事件，共 %d", events);
}

static void t_two_different_keys(void)
{
    key_fsm_t k;
    int events;

    key_fsm_init(&k);
    events  = feed(&k, ADC_VOLU, 15, NULL);
    events += feed(&k, ADC_NONE, 15, NULL);
    events += feed(&k, ADC_VOLD, 15, NULL);
    events += feed(&k, ADC_NONE, 15, NULL);
    events += feed(&k, ADC_VOLU, 15, NULL);

    CHECK_MSG(events == 3, "音量+ / 音量- / 音量+ 序列只识别了 %d 次", events);
}

/* 判键结果必须落在枚举的合法值里，不能出现野值 */
static void t_decode_in_range(void)
{
    uint16_t v;
    int ok = 1;

    for (v = 0; v <= 4095u; v = (uint16_t)(v + 7u)) {
        key_id_t k = key_raw_decode(v);
        if (k != KEY_NONE && k != KEY_PLAY && k != KEY_VOLUP && k != KEY_VOLDOWN) {
            ok = 0;
        }
    }
    CHECK(ok);
}

void test_key_all(void)
{
    RUN(t_thresholds);
    RUN(t_single_event_on_press);
    RUN(t_hold_no_repeat);
    RUN(t_release_allows_retrigger);
    RUN(t_glitch_rejected);
    RUN(t_two_different_keys);
    RUN(t_decode_in_range);
}
