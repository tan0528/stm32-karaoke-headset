#include "sim_engine.h"

#include <math.h>
#include <string.h>

#include "audio_calc.h"
#include "audio_core.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FS_HI    2400000.0
#define FS_MIC     96000.0
#define FS_AUD     48000.0
#define FS_ADC      8000.0
#define PWM_F     100000.0

/* PWM 载波一个周期被切成 24 个仿真步 */
#define HALF_LEN  128            /* DMA 半缓冲点数，与固件一致 */
#define QDEPTH    4              /* 已完成块队列深度 */

/* ---------------------------------------------------------------
 * DMA 模型：双缓冲 + 已完成块 FIFO
 * --------------------------------------------------------------- */
typedef struct {
    uint16_t adc[2][HALF_LEN];
    uint16_t ccr[2][HALF_LEN];
    uint16_t q[QDEPTH][HALF_LEN];
    int      q_head;
    int      q_tail;
    int      q_count;
    int      q_idx;
    long     dropped;
    long     underrun;
    uint16_t q8_gain;
} dma_t;

static void dma_reset(dma_t *d, uint16_t q8_gain)
{
    memset(d, 0, sizeof(*d));
    d->q8_gain = q8_gain;
}

/* 一次半缓冲完成：CPU 处理这一块，然后挂到队列上 */
static void dma_half_complete(dma_t *d, int half)
{
    audio_block_process(d->adc[half], d->ccr[half], HALF_LEN, d->q8_gain);

    if (d->q_count < QDEPTH) {
        memcpy(d->q[d->q_head], d->ccr[half], sizeof(d->ccr[half]));
        d->q_head = (d->q_head + 1) % QDEPTH;
        d->q_count++;
    } else {
        /* CPU 没跟上，这一块还没被消费就被 DMA 覆写了 —— 听感是断音 */
        d->dropped++;
    }
}

/* 消费端：每次 TIM3 更新事件取一个占空比，取不到就维持中点 */
static uint16_t dma_pop(dma_t *d)
{
    uint16_t v;

    if (d->q_count == 0) {
        d->underrun++;
        return (uint16_t)(PWM_PERIOD / 2u);
    }

    v = d->q[d->q_tail][d->q_idx];
    if (++d->q_idx >= HALF_LEN) {
        d->q_idx = 0;
        d->q_tail = (d->q_tail + 1) % QDEPTH;
        d->q_count--;
    }
    return v;
}

/* ---------------------------------------------------------------
 * 主循环
 * --------------------------------------------------------------- */
long sim_run(const float *mic, long n_mic,
             const float *music, long n_music,
             float *out, long out_cap,
             const sim_params_t *p, sim_stats_t *st,
             float *probe, long probe_cap, long *probe_rows)
{
    dma_t  d;
    long   n_hi, i, n_out;
    long   adc_fill, fill_half;
    long   adc_ticks;
    long   probe_n;
    double rpot, g_music;
    double dt, tau, alpha, a_out;
    double phase, v_rc, v_pwm, duty, ccr_now;
    double clip_cnt, peak, next_free_t;
    double y1, y2, y3, y4, z1, z2, z3, z4;
    uint8_t code;

    if (mic == NULL || out == NULL || p == NULL) { return -1; }
    if (n_mic <= 0 || out_cap <= 0)              { return -1; }

    n_hi = n_mic * SIM_OS_MIC;

    /* ---- 数字电位器 -> 加法器增益 ---- */
    code    = volume_level_to_code(p->level);
    rpot    = (double)digipot_ohm(code);
    g_music = R_FEEDBACK / (R_MUSIC_SER + rpot);

    /* ---- RC 一阶低通：离散化后与连续 RC 严格等效 ---- */
    tau   = RC_R * RC_C;
    dt    = 1.0 / FS_HI;
    alpha = dt / (tau + dt);

    /* 输出抽取前的 4 级一阶低通（20kHz），避免 48kHz 抽取混叠 */
    a_out = exp(-2.0 * M_PI * 20000.0 * dt);

    dma_reset(&d, p->q8_gain);

    phase       = 0.0;
    v_rc        = V_BIAS_ADC;
    ccr_now     = (double)(PWM_PERIOD / 2u);
    adc_fill    = 0;
    fill_half   = 0;
    adc_ticks   = 0;
    clip_cnt    = 0.0;
    peak        = 0.0;
    next_free_t = 0.0;
    n_out       = 0;
    probe_n     = (probe != NULL) ? 0 : -1;

    y1 = y2 = y3 = y4 = V_BIAS_SUM;
    z1 = z2 = z3 = z4 = V_BIAS_SUM;

    for (i = 0; i < n_hi; i++) {
        double t = (double)i * dt;
        double music_l = 0.0, music_r = 0.0;
        double v_pre, v_pre_r, vmic_ac;
        long   mi = i / SIM_OS_MIC;

        /* 时间到了就取一帧音乐（NULL 视为静音） */
        if (music != NULL && n_music > 0) {
            long fi = i / SIM_OS_AUD;
            if (fi >= n_music) { fi = n_music - 1; }
            music_l = (double)music[fi * 2];
            music_r = (double)music[fi * 2 + 1];
        }

        /* ================= 1. TIM3 8kHz 更新事件 ================= */
        if (i % SIM_OS_ADC == 0) {
            long raw = (long)lround((double)mic[mi] / V_REF * 4096.0);

            if (raw < 0)    { raw = 0; }
            if (raw > 4095) { raw = 4095; }
            d.adc[fill_half][adc_fill] = (uint16_t)raw;
            adc_fill++;

            if (adc_fill >= HALF_LEN) {
                /* CPU 处理这一块要花 proc_ms，超过 16ms 的窗口就来不及 */
                if (t >= next_free_t) {
                    dma_half_complete(&d, (int)fill_half);
                    next_free_t = t + p->proc_ms * 1e-3;
                } else {
                    d.dropped++;
                }
                adc_fill = 0;
                fill_half ^= 1;
            }

            ccr_now = (double)dma_pop(&d);   /* 占空比按 8kHz 更新 */
            adc_ticks++;
        }

        /* ================= 2. TIM2 100kHz 载波 ================= */
        phase += PWM_F * dt;
        if (phase >= 1.0) { phase -= 1.0; }
        duty  = ccr_now / (double)PWM_PERIOD;
        v_pwm = (phase < duty) ? V_REF : 0.0;

        /* ================= 3. RC 重构 ================= */
        v_rc += alpha * (v_pwm - v_rc);

        /* ================= 4. 运放加法器 ================= */
        vmic_ac = v_rc - V_BIAS_ADC;

        /* 音乐各走自己的声道；麦克风是单声道，同时混进两边 */
        v_pre   = V_BIAS_SUM - (g_music * music_l + (R_FEEDBACK / R_MIC_SER) * vmic_ac);
        v_pre_r = V_BIAS_SUM - (g_music * music_r + (R_FEEDBACK / R_MIC_SER) * vmic_ac);

        if (v_pre   < V_PRE_MIN) { v_pre   = V_PRE_MIN; clip_cnt += 1.0; }
        if (v_pre   > V_PRE_MAX) { v_pre   = V_PRE_MAX; clip_cnt += 1.0; }
        if (v_pre_r < V_PRE_MIN) { v_pre_r = V_PRE_MIN; clip_cnt += 1.0; }
        if (v_pre_r > V_PRE_MAX) { v_pre_r = V_PRE_MAX; clip_cnt += 1.0; }
        {
            double d_l = fabs(v_pre   - V_BIAS_SUM);
            double d_r = fabs(v_pre_r - V_BIAS_SUM);
            if (d_l > peak) { peak = d_l; }
            if (d_r > peak) { peak = d_r; }
        }

        /* ================= 5. 波形探针 ================= */
        if (probe != NULL && p->probe_start >= 0 &&
            i >= p->probe_start && i < p->probe_start + p->probe_len) {
            if (probe_n < probe_cap) {
                float *row = probe + probe_n * SIM_PROBE_COLS;
                row[0] = (float)(t * 1e6);
                row[1] = (float)v_pwm;
                row[2] = (float)v_rc;
                row[3] = (float)music_l;
                row[4] = (float)v_pre;
                row[5] = (float)v_pre_r;
                probe_n++;
            }
        }

        /* ================= 6. 抽取到 48kHz ================= */
        y1 = (1.0 - a_out) * v_pre   + a_out * y1;
        y2 = (1.0 - a_out) * y1      + a_out * y2;
        y3 = (1.0 - a_out) * y2      + a_out * y3;
        y4 = (1.0 - a_out) * y3      + a_out * y4;

        z1 = (1.0 - a_out) * v_pre_r + a_out * z1;
        z2 = (1.0 - a_out) * z1      + a_out * z2;
        z3 = (1.0 - a_out) * z2      + a_out * z3;
        z4 = (1.0 - a_out) * z3      + a_out * z4;

        if (i % SIM_OS_AUD == 0 && n_out < out_cap) {
            out[n_out * 2]     = (float)(y4 - V_BIAS_SUM);
            out[n_out * 2 + 1] = (float)(z4 - V_BIAS_SUM);
            n_out++;
        }
    }

    if (st != NULL) {
        st->digipot_code   = code;
        st->digipot_ohm    = rpot;
        st->music_gain     = g_music;
        st->mic_gain       = R_FEEDBACK / R_MIC_SER;
        st->adc_ticks      = adc_ticks;
        st->blocks_dropped = d.dropped;
        st->fifo_underrun  = d.underrun;
        st->clip_ratio     = clip_cnt / (2.0 * (double)n_hi);
        st->peak_swing     = peak;
        st->out_frames     = n_out;
    }
    if (probe_rows != NULL) {
        *probe_rows = (probe_n > 0) ? probe_n : 0;
    }

    return n_out;
}
