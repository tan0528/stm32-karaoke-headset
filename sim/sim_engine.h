/*
 * sim_engine.h -- PC 端"数字孪生"的纯计算内核
 *
 * 它与 sim_cli.c 分开，是为了让测试能直接拿到缓冲区，
 * 不必经过文件读写，也不需要 numpy。
 *
 * 被替代的不是固件，而是【板子和外设】：
 *   TIM3 的 8kHz 更新事件  -> 时间循环里的 tick 判断
 *   ADC + DMA 循环缓冲      -> 半缓冲/全缓冲回调 + 两级 FIFO
 *   TIM2 的 100kHz PWM      -> 2.4MHz 时间步上的方波
 *   RC 低通重构             -> 一阶 IIR（与连续 RC 严格等效）
 *   运放加法器 + 数字电位器 -> 虚地求和，增益随抽头阻值变化
 *   TDA2822 的供电轨        -> 输出限幅
 *
 * 固件那部分一行不改，直接调用 firmware/audio_core.c。
 */
#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

#include <stdint.h>

/* 各域之间的过采样比（FS_HI = 2.4MHz） */
#define SIM_OS_MIC   25     /* 2400k / 96k  */
#define SIM_OS_AUD   50     /* 2400k / 48k  */
#define SIM_OS_ADC  300     /* 2400k / 8k   */

#define SIM_PROBE_COLS  6   /* t_us, v_pwm, v_rc, music_l, v_pre_l, v_pre_r */

typedef struct {
    uint8_t  level;         /* 音量档位 0..100 */
    uint16_t q8_gain;       /* 麦克风软件增益，Q8，256 = 1.0x */
    double   proc_ms;       /* 每 128 点块的 CPU 耗时（模拟实时性） */
    long     probe_start;   /* 探针起点（FS_HI 索引），<0 表示不采样 */
    long     probe_len;     /* 探针长度（FS_HI 采样点） */
} sim_params_t;

typedef struct {
    uint8_t digipot_code;
    double  digipot_ohm;
    double  music_gain;     /* |R1/(R8+Rw)| */
    double  mic_gain;       /* R1/R16 */
    long    adc_ticks;      /* 实际发生的 8kHz 采样次数 */
    long    blocks_dropped; /* CPU 来不及处理、被 DMA 覆写的块 */
    long    fifo_underrun;  /* 消费端取不到数据的 tick 数 */
    double  clip_ratio;     /* 加法器输出撞到供电轨的样本占比 */
    double  peak_swing;     /* 相对虚地的最大摆幅（V） */
    long    out_frames;     /* 写出的立体声帧数 */
} sim_stats_t;

/*
 * 跑一次完整链路。
 *
 *   mic        : 麦克风前置放大后的模拟信号，96kHz，围绕 V_BIAS_ADC
 *   n_mic      : 上面的点数，决定仿真总时长（1 点 = 25 个 2.4MHz 步）
 *   music      : 外部播放器送来的立体声音乐，48kHz，交织 L/R，围绕 0V
 *   n_music    : 上面的帧数（不足时自动钳到最后一帧，NULL 视为静音）
 *   out        : 输出立体声，48kHz，交织 L/R，已去直流，单位 V
 *   out_cap    : out 能容纳的帧数
 *   p          : 参数
 *   st         : 统计输出，可为 NULL
 *   probe      : 波形探针，每行 SIM_PROBE_COLS 个 float，可为 NULL
 *   probe_cap  : probe 能容纳的行数
 *   probe_rows : 实际写出的行数，可为 NULL
 *
 * 返回写出的帧数，负数表示参数错误。
 */
long sim_run(const float *mic, long n_mic,
             const float *music, long n_music,
             float *out, long out_cap,
             const sim_params_t *p, sim_stats_t *st,
             float *probe, long probe_cap, long *probe_rows);

#endif /* SIM_ENGINE_H */
