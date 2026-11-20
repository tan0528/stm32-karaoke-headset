/*
 * main.c -- 目标板入口（示例）
 *
 * 只演示"各部分怎么接起来"，CubeMX 生成的外设初始化函数在这里只做声明。
 * 实际工程里把这段拷进 CubeMX 生成工程的 main.c，保留 MX_xxx_Init() 调用即可。
 */
#include "stm32f1xx_hal.h"

#include "app_audio.h"
#include "app_control.h"
#include "board_pins.h"
#include "drv_cd4066.h"
#include "drv_mcp41010.h"

/* CubeMX 生成 */
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_DMA_Init(void);
void MX_ADC1_Init(void);
void MX_TIM2_Init(void);
void MX_TIM3_Init(void);
void MX_I2C1_Init(void);

/* 接你手上的 OLED 驱动；没有屏幕就传 NULL */
static void ui_show(const app_state_t *st)
{
    /* 例：
     *   OLED_Clear();
     *   OLED_ShowString(0, 0, st->karaoke ? "KTV" : "MUSIC");
     *   OLED_ShowNum(0, 2, st->volume, 3);
     *   OLED_ShowString(0, 4, st->muted ? "MUTE" : "     ");
     *   OLED_ShowString(0, 6, (st->output == OUTPUT_SPEAKER) ? "SPK" : "HP");
     *   OLED_Refresh();
     */
    (void)st;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC1_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_I2C1_Init();

    cd4066_init();          /* 上电先全断，避免开机爆音 */
    mcp41010_init();

    app_audio_init();
    app_control_init();
    app_control_set_display_cb(ui_show);

    app_audio_start();      /* 载波 -> DMA -> 8kHz 时基 */

    for (;;) {
        /* 主循环只做低优先级的事。
         * 按键扫描的 1ms 节拍在生产代码里应由 TIM6 中断驱动，
         * 在 HAL_TIM_PeriodElapsedCallback 的 TIM6 分支里调
         * app_control_tick_1ms()；这个示例为了少一个外设，
         * 直接在主循环里轮询，逻辑等价。 */
        HAL_Delay(1);
        app_control_tick_1ms();
    }
}
