/*
 * audio_core.c -- 平台的无关音频处理核心（实现）
 *
 * 约束：不含浮点、不含 HAL、不含动态内存。
 * 这份文件是整个项目里唯一同时跑在 ISR 和 PC 上的代码，
 * 所以它必须是纯函数、可重入、无全局状态。
 */
#include "audio_core.h"

/* ============================================================
 * 1. 麦克风支路的块处理
 *
 * 关键点在于"限幅"这一步的位置：必须在回偏置之前。
 * 若省略，q8_gain > 256 时正半周会绕过 4095 后在 uint16_t
 * 里回绕成小值，听感是"噼啪"的数字爆音 —— 比模拟削顶难听得多。
 * ============================================================ */
void audio_block_process(const uint16_t *adc_in, uint16_t *ccr_out,
                         uint16_t n, uint16_t q8_gain)
{
    uint16_t i;

    for (i = 0u; i < n; i++) {
        int32_t s = (int32_t)adc_in[i] - (int32_t)ADC_MID;   /* -2048 .. 2047 */

        s = (s * (int32_t)q8_gain) >> 8;                     /* Q8 定点增益 */

        if (s > 2047)  { s = 2047; }                         /* 限幅 */
        if (s < -2048) { s = -2048; }

        {
            uint32_t u = (uint32_t)(s + (int32_t)ADC_MID);   /* 0 .. 4095 */
            /* 4096 级 -> 720 级：右移 12 位，等价于 /4096，无需除法 */
            ccr_out[i] = (uint16_t)((u * PWM_PERIOD) >> 12);
        }
    }
}

/* ============================================================
 * 2. 音量映射
 *
 * 两个容易写反的地方，都在这里固定下来：
 *
 * (a) 方向。本电路的 MCP41010 是【串联变阻器】接法（A 端悬空，
 *     只用 B-W 两端），串在 R8 与加法器虚地之间。阻值越大 ->
 *     输入电阻越大 -> 增益越低。所以"档位越高"必须对应"抽头码越小"。
 *     写成同相的话，现象是按音量+ 反而变小声。
 *
 * (b) 曲线。见头文件说明：本拓扑下增益已近似对数，线性映射
 *     反而更接近"每档等 dB"。三次方版本保留供对比。
 * ============================================================ */
uint8_t volume_level_to_code_linear(uint8_t level)
{
    if (level > 100u) { level = 100u; }
    /* + 50 做四舍五入，避免整段刻度被系统性压向低码值 */
    return (uint8_t)(((100u - level) * 255u + 50u) / 100u);
}

uint8_t volume_level_to_code_cubic(uint8_t level)
{
    uint32_t t;

    if (level > 100u) { level = 100u; }

    t = (uint32_t)(100u - level);           /* 反相：档位越高，阻值越小 */
    t = t * t * t;                          /* 三次方，100^3 = 1e6 */
    return (uint8_t)((t * 255u) / 1000000u);
}

uint8_t volume_level_to_code(uint8_t level)
{
#if VOLUME_MAP == VOLUME_MAP_CUBIC
    return volume_level_to_code_cubic(level);
#else
    return volume_level_to_code_linear(level);
#endif
}

uint32_t digipot_ohm(uint8_t code)
{
    /* MCP41010: R_WB = (code/256) * R_AB + R_wiper
     * R_AB = 10kΩ, R_wiper 典型 52Ω（datasheet 抽头电阻） */
    return 52u + ((uint32_t)code * (10000u - 52u)) / 256u;
}

/* ============================================================
 * 3. 线控按键
 *
 * 阈值按 12bit ADC 计数给出，配合耳机座 M 脚上的 R14 = 1k 下拉。
 * 区间边界留得比较宽，因为不同耳机的按键阻值离散性很大。
 * ============================================================ */
key_id_t key_raw_decode(uint16_t adc_now)
{
    if (adc_now > 3600u) { return KEY_NONE;     }   /* 无按键，接近 3.3V */
    if (adc_now > 2600u) { return KEY_PLAY;     }   /* 播放 / 暂停 */
    if (adc_now > 1400u) { return KEY_VOLUP;    }   /* 音量 + */
    if (adc_now >  400u) { return KEY_VOLDOWN;  }   /* 音量 - */
    return KEY_NONE;                                /* 短路 / 异常 */
}

void key_fsm_init(key_fsm_t *k)
{
    k->cand    = KEY_NONE;
    k->cnt     = 0u;
    k->stable  = KEY_NONE;
    k->latched = 0u;
}

/* 每 1ms 调用一次。连续 5 次（5ms）一致才认，且必须在松开后
 * 才能上报下一次按下沿 —— 否则长按会被当成连击。 */
key_id_t key_fsm_update(key_fsm_t *k, uint16_t adc_now)
{
    key_id_t raw = key_raw_decode(adc_now);

    if (raw != k->cand) {
        k->cand = raw;
        k->cnt  = 0u;
        return KEY_NONE;
    }

    if (k->cnt < 6u) { k->cnt++; }

    if ((k->cnt >= 5u) && (raw != k->stable)) {
        k->stable = raw;
        if (raw == KEY_NONE) {
            k->latched = 0u;        /* 松开，允许下一次上报 */
        }
        return KEY_NONE;
    }

    if ((k->stable != KEY_NONE) && (k->latched == 0u) && (k->cnt >= 5u)) {
        k->latched = 1u;
        return k->stable;           /* 返回按下沿事件 */
    }

    return KEY_NONE;
}
