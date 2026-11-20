/*
 * test_audio_core.c -- 块处理：增益、限幅、映射
 *
 * 这组测试盯的是三件事：
 *   1. 恒等通路（增益 1.0）必须精确等于理论映射值，不能有累积偏差；
 *   2. 增益 > 1 时必须削顶而不是整数回绕（回绕的听感是"噼啪"爆音）；
 *   3. 输出恒在 0..PWM_ARR，任何输入都不能越界 —— 越界会改坏
 *      TIM2 的比较寄存器，轻则破音重则丢输出。
 */
#include "audio_core.h"
#include "test_util.h"

/* 理论映射：12bit -> 720 级 */
static uint16_t expect_ccr(uint16_t adc, uint16_t q8_gain)
{
    int32_t s = (int32_t)adc - (int32_t)ADC_MID;

    s = (s * (int32_t)q8_gain) >> 8;
    if (s > 2047)  { s = 2047; }
    if (s < -2048) { s = -2048; }
    return (uint16_t)(((uint32_t)(s + (int32_t)ADC_MID) * PWM_PERIOD) >> 12);
}

static void t_identity(void)
{
    uint16_t in[16], out[16];
    uint16_t i;

    for (i = 0; i < 16u; i++) {
        in[i] = (uint16_t)(i * 273u);        /* 0, 273, ... 4095 */
    }
    audio_block_process(in, out, 16u, 256u);

    for (i = 0; i < 16u; i++) {
        CHECK_NEAR(out[i], expect_ccr(in[i], 256u), 0.0);
    }

    /* 几个有物理意义的点 */
    CHECK_NEAR(out[0],  0.0,   0.0);   /* 0V      -> 全低 */
    CHECK_NEAR(out[15], PWM_ARR, 0.0); /* 满量程   -> 全高 */
}

static void t_midpoint(void)
{
    uint16_t in[1] = { ADC_MID };
    uint16_t out[1];

    audio_block_process(in, out, 1u, 256u);
    CHECK_NEAR(out[0], 360.0, 0.0);    /* 2048*720/4096 = 360，正好 50% */
}

static void t_gain_2x(void)
{
    uint16_t in[2] = { (uint16_t)(ADC_MID + 100u), (uint16_t)(ADC_MID - 100u) };
    uint16_t out[2];

    audio_block_process(in, out, 2u, 512u);
    /* +100 经 2x 变 +200 -> 2248 -> 395；对称地 -200 -> 1848 -> 324 */
    CHECK_NEAR(out[0], 395.0, 0.0);
    CHECK_NEAR(out[1], 324.0, 0.0);
    /* 两个方向的增量必须一样大，否则有直流偏移 */
    CHECK_NEAR((double)(out[0] - 360), (double)(360 - out[1]), 1.0);
}

static void t_clip_positive(void)
{
    uint16_t in[1] = { 4095u };
    uint16_t out[1];

    audio_block_process(in, out, 1u, 1024u);      /* 4x */
    CHECK_NEAR(out[0], PWM_ARR, 0.0);             /* 顶上，不是回绕成 0 */
}

static void t_clip_negative(void)
{
    uint16_t in[1] = { 0u };
    uint16_t out[1];

    audio_block_process(in, out, 1u, 1024u);
    CHECK_NEAR(out[0], 0.0, 0.0);                 /* 沉底，不是跳成 719 */
}

static void t_mute_gain_zero(void)
{
    uint16_t in[8];
    uint16_t out[8];
    uint16_t i;

    for (i = 0; i < 8u; i++) {
        in[i] = (uint16_t)(i * 500u);
    }
    audio_block_process(in, out, 8u, 0u);

    for (i = 0; i < 8u; i++) {
        CHECK_NEAR(out[i], 360.0, 0.0);   /* 增益 0 -> 恒为静音电平 */
    }
}

static void t_never_out_of_range(void)
{
    static const uint16_t gains[] = { 0u, 1u, 128u, 256u, 512u, 1023u, 4095u };
    uint16_t in[64], out[64];
    uint16_t g, i;
    int ok = 1;

    for (g = 0; g < (uint16_t)(sizeof(gains) / sizeof(gains[0])); g++) {
        for (i = 0; i < 64u; i++) {
            in[i] = (uint16_t)(i * 65u);   /* 覆盖 0..4095 */
        }
        audio_block_process(in, out, 64u, gains[g]);
        for (i = 0; i < 64u; i++) {
            if (out[i] > PWM_ARR) { ok = 0; }
        }
    }
    CHECK(ok);
}

/* 单调性：输入变大，输出不能变小（否则波形会有折点） */
static void t_monotonic(void)
{
    uint16_t in[4096], out[4096];
    uint16_t i;
    int ok = 1;

    for (i = 0; i < 4096u; i++) {
        in[i] = i;
    }
    audio_block_process(in, out, 4096u, 768u);    /* 3x，会进限幅区 */

    for (i = 1; i < 4096u; i++) {
        if (out[i] < out[i - 1u]) { ok = 0; }
    }
    CHECK(ok);
}

void test_audio_core_all(void)
{
    RUN(t_identity);
    RUN(t_midpoint);
    RUN(t_gain_2x);
    RUN(t_clip_positive);
    RUN(t_clip_negative);
    RUN(t_mute_gain_zero);
    RUN(t_never_out_of_range);
    RUN(t_monotonic);
}
