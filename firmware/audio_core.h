/*
 * audio_core.h -- 与平台无关的音频处理核心
 *
 * 本文件在 STM32 目标板上与 PC 仿真中编译的是【同一份代码】，
 * 不含任何 HAL / CMSIS 调用，只用 <stdint.h>。
 *
 * 上板时由 HAL_ADC_ConvHalfCpltCallback / HAL_ADC_ConvCpltCallback 调用；
 * 仿真时由 sim/sim_engine.c 里的假 DMA 回调调用。
 */
#ifndef AUDIO_CORE_H
#define AUDIO_CORE_H

#include <stdint.h>

/* ---------------------------------------------------------------
 * 与 CubeMX 配置一一对应的常量
 * --------------------------------------------------------------- */
#define ADC_MID        2048u   /* 12bit ADC 量程中点，对应前端 1.65V 偏置 */
#define PWM_ARR        719u    /* TIM2: PSC=0, ARR=719 -> 100kHz @72MHz */
#define PWM_PERIOD     (PWM_ARR + 1u)                  /* 720 级占空比 */

/* ---------------------------------------------------------------
 * 麦克风支路的块处理
 *
 * 四步：去偏置 -> 软件增益 -> 限幅 -> 回偏置 -> 映射比较值
 * 全整数运算：Cortex-M3 无 FPU,在 16ms 窗口内跑 128 点浮点会直接超时。
 *
 *   adc_in  : DMA 搬进来的原始 12bit 采样
 *   ccr_out : 映射后的 TIM2 比较值，恒在 0..PWM_ARR
 *   n       : 本次处理点数（半缓冲 128 / 全缓冲 128）
 *   q8_gain : Q8 定点软件增益，256 = 1.0x
 * --------------------------------------------------------------- */
void audio_block_process(const uint16_t *adc_in, uint16_t *ccr_out,
                         uint16_t n, uint16_t q8_gain);

/* ---------------------------------------------------------------
 * 数字音量：人机档位 <-> MCP41010 抽头码
 *
 * 这里有两个映射，默认用线性，理由见下面 t_linear_beats_cubic 的测试
 * 与 docs/architecture.md 第 3 节：
 *
 *   常见做法是按响度幂律做三次方映射（VOLUME_MAP_CUBIC）。那条经验
 *   成立的前提是"增益正比于抽头码"，也就是电位器当分压器用。
 *   但本电路是【串联变阻器】接法：g = R1 / (R8 + R_WB)，
 *   R_WB 是抽头码的线性函数，所以 g 本来就随码值呈双曲（近似对数）变化。
 *   再叠一次三次方，等于对数量化两遍：实测与"每档等 dB"的均匀刻度相比
 *   RMS 偏差 1.99dB，且最后 25% 的行程只变化 0.20dB（等于空行程）。
 *   换成线性映射后偏差降到 0.91dB。
 *
 * 两个函数都保留，方便对比与回归；volume_level_to_code 按 VOLUME_MAP 分派。
 * --------------------------------------------------------------- */
#define VOLUME_MAP_LINEAR  0
#define VOLUME_MAP_CUBIC   1

#ifndef VOLUME_MAP
#define VOLUME_MAP VOLUME_MAP_LINEAR
#endif

uint8_t  volume_level_to_code(uint8_t level);          /* 受 VOLUME_MAP 控制 */
uint8_t  volume_level_to_code_linear(uint8_t level);
uint8_t  volume_level_to_code_cubic(uint8_t level);

/* 抽头码 -> W-B 等效阻值(Ω) */
uint32_t digipot_ohm(uint8_t code);

/* 注：涉及浮点的增益/dB 计算放在 audio_calc.h，不进中断路径 */

/* ---------------------------------------------------------------
 * 线控按键：模拟阈值判键 + 消抖状态机
 *
 * 四段耳机在 MIC 线上串不同阻值，按下时把分压点拉低，
 * 所以按键是模拟量，必须用阈值区间而不是高低电平。
 * --------------------------------------------------------------- */
typedef enum {
    KEY_NONE = 0,
    KEY_PLAY,
    KEY_VOLUP,
    KEY_VOLDOWN
} key_id_t;

typedef struct {
    key_id_t cand;      /* 本次判断出的候选键 */
    uint8_t  cnt;       /* 连续命中次数 */
    key_id_t stable;    /* 已确认的稳定键 */
    uint8_t  latched;   /* 已上报过按下沿，等松开 */
} key_fsm_t;

void      key_fsm_init(key_fsm_t *k);
key_id_t  key_fsm_update(key_fsm_t *k, uint16_t adc_now);
key_id_t  key_raw_decode(uint16_t adc_now);   /* 单次判键，无消抖 */

#endif /* AUDIO_CORE_H */
