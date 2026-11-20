/*
 * drv_cd4066.h -- CD4066 四路双向模拟开关
 *
 * 两颗芯片的分工：
 *   U1  A/B 组 -> 串在 L->LI、R->RI 主通路里，由 PA1 同时驱动
 *                （所以两个声道总是一起通断，没有单声道静音）
 *       C   组 -> 把 MIC 接到 PA0，由 PB10 控制，是 K 歌支路的总闸
 *       D   组 -> 空置
 *   U2  A/B 组 -> L2->L3、R2->R3，送耳机座，PA3 控制
 *       C/D 组 -> L2、R2 汇成 RL 单声道送功放，PA2 控制
 *
 * 注意：耳机与扬声器是"两路独立使能"，不是严格的二选一，
 * 只是软件上按互斥的方式操作。两者同时导通会把两路输出并在一起。
 */
#ifndef DRV_CD4066_H
#define DRV_CD4066_H

#include <stdint.h>

typedef enum {
    OUTPUT_HEADPHONE = 0,
    OUTPUT_SPEAKER,
    OUTPUT_BOTH          /* 调试用，实际不会同时接负载 */
} output_sel_t;

void cd4066_init(void);

/* 主通路通断：pass=0 时切断 L/R，实现一键静音（音量值保持不变） */
void cd4066_set_main_pass(int pass);

/* K 歌支路：把麦克风接到 ADC 输入 */
void cd4066_set_mic_path(int on);

/* 输出选择 */
void cd4066_set_output(output_sel_t sel);

#endif /* DRV_CD4066_H */
