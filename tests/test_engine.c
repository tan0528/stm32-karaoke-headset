/*
 * test_engine.c -- 端到端：把 sim_run 当成"板子"来验证
 *
 * 这组测试是项目的核心证据：时基对不对、启动延迟多少、
 * 增益算得准不准、声道会不会串、8kHz 采样会折返成什么、
 * CPU 跟不上时会发生什么 —— 全部在 PC 上量出来，不需要板子。
 *
 * 测量用整数周期 DFT（矩形窗），因为测试信号都是自己合成的，
 * 频率可以取成严格落在频点上，不需要窗函数修正。
 */
#include "sim_engine.h"

#include <string.h>

#include "audio_calc.h"
#include "test_util.h"

#define FS_MIC   96000
#define FS_AUD   48000
#define NSEC     1
#define NMIC     (FS_MIC * NSEC)
#define NFRAME   (FS_AUD * NSEC)

/* 静态分配：每个约 384KB，放 BSS 比 malloc 更容易看出内存占用 */
static float g_mic[NMIC];
static float g_mus[NFRAME * 2];
static float g_out[NFRAME * 2];

/* ---------------------------------------------------------------
 * 信号合成
 * --------------------------------------------------------------- */
static void fill_dc(float *x, long n, double v)
{
    long i;
    for (i = 0; i < n; i++) { x[i] = (float)v; }
}

static void fill_sine(float *x, long n, double fs, double f, double amp, double dc)
{
    long i;
    for (i = 0; i < n; i++) {
        x[i] = (float)(dc + amp * sin(2.0 * M_PI * f * (double)i / fs));
    }
}

/* 立体声音乐：左声道单音，右声道静音（用于验证声道隔离） */
static void music_left_only(float *x, long frames, double f, double amp)
{
    long i;
    for (i = 0; i < frames; i++) {
        x[i * 2]     = (float)(amp * sin(2.0 * M_PI * f * (double)i / FS_AUD));
        x[i * 2 + 1] = 0.0f;
    }
}

/* ---------------------------------------------------------------
 * 单频幅度：要求 f * n / fs 为整数，否则有泄漏
 * --------------------------------------------------------------- */
static double tone_amp(const float *x, int ch, long start, long n, double f, double fs)
{
    double re = 0.0, im = 0.0;
    long   k;

    for (k = 0; k < n; k++) {
        double v  = (double)x[(start + k) * 2 + ch];
        double ph = 2.0 * M_PI * f * (double)(start + k) / fs;
        re += v * cos(ph);
        im -= v * sin(ph);
    }
    return 2.0 * sqrt(re * re + im * im) / (double)n;
}

static double peak_abs(const float *x, long start, long frames)
{
    double m = 0.0;
    long   i;

    for (i = start; i < start + frames; i++) {
        double a = fabs((double)x[i * 2]);
        double b = fabs((double)x[i * 2 + 1]);
        if (a > m) { m = a; }
        if (b > m) { m = b; }
    }
    return m;
}

static double rms(const float *x, int ch, long start, long frames)
{
    double s = 0.0;
    long   i;

    for (i = start; i < start + frames; i++) {
        double v = (double)x[i * 2 + ch];
        s += v * v;
    }
    return sqrt(s / (double)frames);
}

/* 一次标准运行的封装 */
static long run_default(uint8_t level, uint16_t gain, double proc_ms,
                        const float *music, long n_music, sim_stats_t *st)
{
    sim_params_t p;

    memset(&p, 0, sizeof(p));
    p.level       = level;
    p.q8_gain     = gain;
    p.proc_ms     = proc_ms;
    p.probe_start = -1;          /* 不要波形探针 */
    p.probe_len   = 0;

    return sim_run(g_mic, NMIC, music, n_music, g_out, NFRAME, &p, st,
                   NULL, 0, NULL);
}

/* ===============================================================
 * 1. 时基
 * =============================================================== */
static void t_tick_rate_is_8k(void)
{
    sim_stats_t st;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    memset(&st, 0, sizeof(st));

    CHECK(run_default(100u, 256u, 2.0, NULL, 0, &st) == NFRAME);
    CHECK_MSG(st.adc_ticks == 8000, "1 秒内 ADC 触发了 %ld 次", st.adc_ticks);
    CHECK_MSG(st.out_frames == 48000, "输出 %ld 帧", st.out_frames);
}

static void t_startup_latency(void)
{
    sim_stats_t st;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, NULL, 0, &st);

    /* 第一个半缓冲要攒满 128 点（16ms）才能处理，期间消费端空转 127 次 */
    CHECK_MSG(st.fifo_underrun == 127, "启动期 underrun = %ld，应 127", st.fifo_underrun);
    CHECK(st.blocks_dropped == 0);
}

/* ===============================================================
 * 2. 静态精度
 * =============================================================== */
static void t_dc_input_gives_zero_output(void)
{
    sim_stats_t st;
    double     steady;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);   /* 恰好是 ADC 中点 2048 */
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, NULL, 0, &st);

    /* 前几帧有约 0.19V 的启动瞬态：RC 的初始电荷不等于稳态纹波的相位，
     * 20kHz 抽取滤波器用 30µs 左右把它吃掉，属于物理过程不是 bug。
     * 所以从 10ms 起测量稳态值。 */
    steady = peak_abs(g_out, 480, NFRAME - 480);

    CHECK_MSG(steady < 2e-3, "直流输入在稳态残留 %.5f V", steady);
    CHECK_MSG(st.clip_ratio == 0.0, "直流输入不应触发削顶");
}

/* 上一条的对照：把头 10ms 也算进去，瞬态确实是可观测的，
 * 说明"开机轻微噗声"在原理上就存在，只能靠软启动或静音延时消除。 */
static void t_startup_transient_exists(void)
{
    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    (void)run_default(100u, 256u, 2.0, NULL, 0, NULL);

    CHECK(peak_abs(g_out, 0, 480) > 0.05);
}

static void t_music_gain(void)
{
    sim_stats_t st;
    double     expect, got;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    music_left_only(g_mus, NFRAME, 1000.0, 0.2);
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, g_mus, NFRAME, &st);

    /* 最高档：R_WB = 52Ω，增益 = 20k / (5k + 52) */
    expect = 0.2 * (R_FEEDBACK / (R_MUSIC_SER + 52.0));
    got    = tone_amp(g_out, 0, 4800, 4800, 1000.0, FS_AUD);

    CHECK_MSG(fabs(got - expect) / expect < 0.02,
              "1kHz 幅度 %.4f V，期望 %.4f V（偏差 %.1f%%）",
              got, expect, 100.0 * fabs(got - expect) / expect);

    /* 20kHz 四阶低通在 1kHz 处的衰减应小于 0.5% */
    CHECK(got < expect);
}

static void t_channel_isolation(void)
{
    sim_stats_t st;
    double     right;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    music_left_only(g_mus, NFRAME, 1000.0, 0.2);
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, g_mus, NFRAME, &st);

    right = tone_amp(g_out, 1, 4800, 4800, 1000.0, FS_AUD);
    CHECK_MSG(right < 1e-4, "左声道单音在右声道泄漏了 %.6f V", right);
}

/* ===============================================================
 * 3. 8kHz 采样的固有代价
 * =============================================================== */
static void t_aliasing_5k_equals_3k(void)
{
    sim_stats_t st;
    double a3, a5;

    /* 先喂 3kHz（低于 4kHz 的 Nyquist，正常通过） */
    fill_sine(g_mic, NMIC, FS_MIC, 3000.0, 0.25, V_BIAS_ADC);
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, NULL, 0, &st);
    a3 = tone_amp(g_out, 0, 4800, 4800, 3000.0, FS_AUD);

    /* 再喂 5kHz —— 它高于 Nyquist，必然折返到 8k-5k = 3kHz */
    fill_sine(g_mic, NMIC, FS_MIC, 5000.0, 0.25, V_BIAS_ADC);
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, NULL, 0, &st);
    a5 = tone_amp(g_out, 0, 4800, 4800, 3000.0, FS_AUD);

    CHECK_MSG(a3 > 0.3, "3kHz 输入在输出里只有 %.3f V，链路可能不通", a3);
    CHECK_MSG(fabs(a3 - a5) / a3 < 0.03,
              "3kHz 输入 -> %.4f V，5kHz 输入 -> %.4f V，两者应无从区分",
              a3, a5);
}

/* ===============================================================
 * 4. 实时性
 * =============================================================== */
static void t_overrun_when_cpu_too_slow(void)
{
    sim_stats_t st;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    memset(&st, 0, sizeof(st));
    /* 一块 128 点只有 16ms 窗口，这里故意给 18ms */
    (void)run_default(100u, 256u, 18.0, NULL, 0, &st);

    CHECK_MSG(st.blocks_dropped > 0, "CPU 超时却没有丢块");
    CHECK_MSG(st.fifo_underrun > 0, "CPU 超时却没有 underrun");
}

static void t_no_overrun_when_cpu_fast(void)
{
    sim_stats_t st;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, NULL, 0, &st);

    CHECK_MSG(st.blocks_dropped == 0, "2ms 处理时间却丢了 %ld 块", st.blocks_dropped);
}

/* ===============================================================
 * 5. 削顶
 * =============================================================== */
static void t_clipping_detected(void)
{
    sim_stats_t st;

    /* 麦克风输入 1.0V 峰，软件增益 4x -> 4V 摆幅，必然撞到 ±2.1V 的轨 */
    fill_sine(g_mic, NMIC, FS_MIC, 300.0, 1.0, V_BIAS_ADC);
    memset(&st, 0, sizeof(st));
    (void)run_default(100u, 256u, 2.0, NULL, 0, &st);

    CHECK_MSG(st.clip_ratio > 0.01, "明显过载但 clip_ratio = %.4f", st.clip_ratio);
    CHECK_MSG(st.peak_swing <= (V_PRE_MAX - V_BIAS_SUM) + 1e-6,
              "摆幅 %.3f V 超过了输出轨", st.peak_swing);
}

/* ===============================================================
 * 6. 音量端到端
 * =============================================================== */
static void t_volume_increases_energy(void)
{
    static const uint8_t levels[] = { 0u, 25u, 50u, 75u, 100u };
    double prev = -1.0;
    int    ok = 1;
    int    i;

    music_left_only(g_mus, NFRAME, 1000.0, 0.2);

    for (i = 0; i < (int)(sizeof(levels) / sizeof(levels[0])); i++) {
        sim_stats_t st;
        double e;

        fill_dc(g_mic, NMIC, V_BIAS_ADC);
        memset(&st, 0, sizeof(st));
        (void)run_default(levels[i], 256u, 2.0, g_mus, NFRAME, &st);

        e = rms(g_out, 0, 4800, 9600);
        if (e <= prev) { ok = 0; }
        prev = e;
    }
    CHECK_MSG(ok, "音量档位提高后输出能量没有增加");
}

static void t_bad_arguments_rejected(void)
{
    sim_params_t p;

    memset(&p, 0, sizeof(p));
    p.probe_start = -1;

    CHECK(sim_run(NULL, NMIC, NULL, 0, g_out, NFRAME, &p, NULL, NULL, 0, NULL) < 0);
    CHECK(sim_run(g_mic, 0, NULL, 0, g_out, NFRAME, &p, NULL, NULL, 0, NULL) < 0);
    CHECK(sim_run(g_mic, NMIC, NULL, 0, NULL, NFRAME, &p, NULL, NULL, 0, NULL) < 0);
    CHECK(sim_run(g_mic, NMIC, NULL, 0, g_out, NFRAME, NULL, NULL, NULL, 0, NULL) < 0);
}

/* 音乐比麦克风短时必须钳位，不能越界读 */
static void t_short_music_buffer(void)
{
    sim_stats_t st;

    fill_dc(g_mic, NMIC, V_BIAS_ADC);
    music_left_only(g_mus, 4800, 1000.0, 0.2);   /* 只有 0.1 秒 */
    memset(&st, 0, sizeof(st));

    CHECK(run_default(100u, 256u, 2.0, g_mus, 4800, &st) == NFRAME);
}

void test_engine_all(void)
{
    RUN(t_tick_rate_is_8k);
    RUN(t_startup_latency);
    RUN(t_dc_input_gives_zero_output);
    RUN(t_startup_transient_exists);
    RUN(t_music_gain);
    RUN(t_channel_isolation);
    RUN(t_aliasing_5k_equals_3k);
    RUN(t_overrun_when_cpu_too_slow);
    RUN(t_no_overrun_when_cpu_fast);
    RUN(t_clipping_detected);
    RUN(t_volume_increases_energy);
    RUN(t_bad_arguments_rejected);
    RUN(t_short_music_buffer);
}
