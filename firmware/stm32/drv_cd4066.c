/*
 * drv_cd4066.c -- CD4066 模拟开关控制
 *
 * 控制端给高电平导通、低电平断开。这里把"有效电平"抽成宏，
 * 因为不同板子上开关可能接成低有效（中间加了一级反相）。
 */
#include "drv_cd4066.h"

#include "board_pins.h"

/* 1 = 控制端高电平导通 */
#define SW_ACTIVE_HIGH   1

static void sw_write(GPIO_TypeDef *port, uint16_t pin, int on)
{
#if SW_ACTIVE_HIGH
    HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
}

void cd4066_init(void)
{
    /* 上电先全断：避免开机瞬间把未稳定的信号送到耳机 */
    sw_write(SW_MAIN_PORT, SW_MAIN_PIN, 0);
    sw_write(SW_MIC_PORT,  SW_MIC_PIN,  0);
    sw_write(SW_SPK_PORT,  SW_SPK_PIN,  0);
    sw_write(SW_HP_PORT,   SW_HP_PIN,   0);
}

void cd4066_set_main_pass(int pass)
{
    sw_write(SW_MAIN_PORT, SW_MAIN_PIN, pass ? 1 : 0);
}

void cd4066_set_mic_path(int on)
{
    sw_write(SW_MIC_PORT, SW_MIC_PIN, on ? 1 : 0);
}

void cd4066_set_output(output_sel_t sel)
{
    sw_write(SW_HP_PORT,  SW_HP_PIN,  (sel == OUTPUT_HEADPHONE || sel == OUTPUT_BOTH) ? 1 : 0);
    sw_write(SW_SPK_PORT, SW_SPK_PIN, (sel == OUTPUT_SPEAKER   || sel == OUTPUT_BOTH) ? 1 : 0);
}
