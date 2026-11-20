/*
 * sim_cli.c -- 仿真器命令行前端：管文件读写，计算全在 sim_engine.c
 *
 * 用法：
 *   sim mic.raw music.raw out.raw probe.csv level q8_gain proc_ms
 *
 * 所有 .raw 都是 float32 裸数据（小端），单位伏特。
 *   mic   : 96kHz 单声道，围绕 1.65V
 *   music : 48kHz 立体声交织，围绕 0V
 *   out   : 48kHz 立体声交织，已去直流
 *
 * 标准输出是一行一个 key=value，便于 Python 侧解析。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim_engine.h"

#define LEN_AUDIO_SECONDS_CAP 600L   /* 防止误传超大文件把内存吃满 */

static float *load_f32(const char *path, long *count)
{
    FILE *f = fopen(path, "rb");
    long  n;
    float *p;

    if (f == NULL) {
        fprintf(stderr, "sim: 打不开 %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    n = ftell(f) / (long)sizeof(float);
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    p = (float *)malloc(sizeof(float) * (size_t)n);
    if (p == NULL) { fclose(f); return NULL; }

    if (fread(p, sizeof(float), (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "sim: 读 %s 时长度不符\n", path);
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *count = n;
    return p;
}

int main(int argc, char **argv)
{
    float  *mic = NULL, *mus = NULL, *out = NULL, *probe = NULL;
    long    n_mic = 0, n_mus = 0;
    long    n_frames, n_out, probe_rows = 0;
    long    probe_len;
    FILE   *fo;
    sim_params_t p;
    sim_stats_t  st;

    if (argc < 8) {
        fprintf(stderr,
                "usage: %s mic.raw music.raw out.raw probe.csv level q8_gain proc_ms\n",
                argv[0]);
        return 1;
    }

    memset(&p, 0, sizeof(p));
    memset(&st, 0, sizeof(st));

    p.level    = (uint8_t)atoi(argv[5]);
    p.q8_gain  = (uint16_t)atoi(argv[6]);
    p.proc_ms  = atof(argv[7]);

    mic = load_f32(argv[1], &n_mic);
    mus = load_f32(argv[2], &n_mus);
    if (mic == NULL || mus == NULL) { free(mic); free(mus); return 1; }

    if (n_mic > LEN_AUDIO_SECONDS_CAP * 96000L) {
        fprintf(stderr, "sim: 输入过长（>%ld 秒），拒绝加载\n", LEN_AUDIO_SECONDS_CAP);
        free(mic); free(mus);
        return 1;
    }
    n_mus /= 2;                                  /* 立体声帧数 */
    n_frames = n_mic * SIM_OS_MIC / SIM_OS_AUD;  /* 输出帧数 */

    out = (float *)calloc((size_t)n_frames * 2u, sizeof(float));
    if (out == NULL) { free(mic); free(mus); return 1; }

    /* 探针窗口：默认 1.000s 起的 2ms，正好覆盖稳态载波。
     * 可选参数 probe_start(s) probe_len(ms) 可覆盖，便于观察更长的台阶波形。 */
    probe_len      = n_mic * SIM_OS_MIC / 500L;    /* 2ms 的 2.4MHz 点数 */
    p.probe_start  = (long)(1.0 * 2400000.0);
    p.probe_len    = probe_len;
    if (argc >= 9) {
        p.probe_start = (long)(atof(argv[8]) * 2400000.0);
    }
    if (argc >= 10) {
        p.probe_len = (long)(atof(argv[9]) * 2400.0);   /* ms -> 2.4MHz 点数 */
    }
    if (p.probe_start < 0) { p.probe_start = 0; }
    if (p.probe_start + p.probe_len > n_mic * SIM_OS_MIC) {
        p.probe_start = 0;                          /* 输入太短就从头截 */
        p.probe_len   = n_mic * SIM_OS_MIC;
    }
    probe = (float *)calloc((size_t)p.probe_len * SIM_PROBE_COLS, sizeof(float));

    n_out = sim_run(mic, n_mic, mus, n_mus, out, n_frames, &p, &st,
                    probe, p.probe_len, &probe_rows);
    if (n_out < 0) {
        fprintf(stderr, "sim: sim_run 参数错误\n");
        free(mic); free(mus); free(out); free(probe);
        return 1;
    }

    fo = fopen(argv[3], "wb");
    if (fo == NULL) {
        fprintf(stderr, "sim: 写不了 %s\n", argv[3]);
        free(mic); free(mus); free(out); free(probe);
        return 1;
    }
    fwrite(out, sizeof(float), (size_t)n_out * 2u, fo);
    fclose(fo);

    fo = fopen(argv[4], "w");
    if (fo != NULL) {
        long k;
        fprintf(fo, "t_us,v_pwm,v_rc,music,v_pre_l,v_pre_r\n");
        for (k = 0; k < probe_rows; k++) {
            const float *r = probe + k * SIM_PROBE_COLS;
            fprintf(fo, "%.4f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                    r[0], r[1], r[2], r[3], r[4], r[5]);
        }
        fclose(fo);
    }

    printf("level=%u\n",            (unsigned)p.level);
    printf("q8_gain=%u\n",          (unsigned)p.q8_gain);
    printf("digipot_code=%u\n",     (unsigned)st.digipot_code);
    printf("digipot_ohm=%.1f\n",    st.digipot_ohm);
    printf("music_gain=%.4f\n",     st.music_gain);
    printf("mic_gain=%.4f\n",       st.mic_gain);
    printf("adc_ticks=%ld\n",       st.adc_ticks);
    printf("blocks_dropped=%ld\n",  st.blocks_dropped);
    printf("fifo_underrun=%ld\n",   st.fifo_underrun);
    printf("clip_ratio=%.6f\n",     st.clip_ratio);
    printf("peak_swing=%.4f\n",     st.peak_swing);
    printf("out_frames=%ld\n",      st.out_frames);
    printf("proc_ms=%.2f\n",        p.proc_ms);

    free(mic); free(mus); free(out); free(probe);
    return 0;
}
