/*
 * audio_calc.h -- 设计期计算（浮点），与 audio_core.h 的整数核心分开
 *
 * 这里的东西【不进中断】。放在头文件里做成 static inline，是为了让
 * 仿真、测试、离线分析三处用的是同一套系数，不会各写一份漂移掉。
 *
 * 常量取自原理图：
 *   R1  = 20k   加法器反馈电阻
 *   R8  = 5k    左声道输入串阻（右声道 R9 同值）
 *   R16 = 5k    麦克风输入串阻
 *   R2/R3 = 100k/100k  虚地偏置分压 -> 2.5V（5V 供电的一半）
 *   RC  = 2.2k / 10nF  PWM 重构低通
 */
#ifndef AUDIO_CALC_H
#define AUDIO_CALC_H

#include <math.h>

#include "audio_core.h"

/* 严格 C11 下 math.h 不保证暴露 M_PI（MinGW / MSVC 都没有） */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- 模拟域常量（单位：Ω / F / V） ---- */
#define R_FEEDBACK     20000.0
#define R_MUSIC_SER     5000.0
#define R_MIC_SER       5000.0
#define RC_R            2200.0
#define RC_C            10.0e-9

#define V_REF            3.3     /* ADC / PWM 参考与供电 */
#define V_BIAS_ADC       1.65    /* 麦克风前端偏置 = V_REF/2 */
#define V_BIAS_SUM       2.5     /* 加法器虚地 = 5V/2 */
#define V_PRE_MIN        0.4     /* NE5532 单电源实际可用摆幅下限 */
#define V_PRE_MAX        4.6

/* 音乐支路增益的绝对值 |R1 / (R8 + R_WB)| */
static inline double volume_gain(uint8_t code)
{
    return R_FEEDBACK / (R_MUSIC_SER + (double)digipot_ohm(code));
}

/* 人机档位 -> dB（以最高档为 0dB 参考，故恒 <= 0，便于看"掉了多少"） */
static inline double level_to_db(uint8_t level)
{
    double g  = volume_gain(volume_level_to_code(level));
    double g0 = volume_gain(volume_level_to_code(100));
    return 20.0 * log10(g / g0 + 1e-12);
}

/* 一阶 RC 的 -3dB 截止频率 */
static inline double rc_fc(void)
{
    return 1.0 / (2.0 * M_PI * RC_R * RC_C);
}

/* PWM 载波经一阶 RC 后的理论纹波峰峰值（忽略负载） */
static inline double rc_ripple_vpp(double f_pwm)
{
    double w = 2.0 * M_PI * f_pwm;
    double tau = RC_R * RC_C;
    /* 方波基波幅度 4Vdd/π * (1/2)，经 |H| 衰减后峰峰值 = 2*|H|*基波峰值 */
    return 2.0 * (2.0 * V_REF / M_PI) * (1.0 / sqrt(1.0 + (w * tau) * (w * tau)));
}

#endif /* AUDIO_CALC_H */
