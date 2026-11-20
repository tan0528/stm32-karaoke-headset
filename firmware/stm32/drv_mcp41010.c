/*
 * drv_mcp41010.c -- 软件 SPI 驱动 MCP41010
 *
 * 为什么是软件时序而不是硬件 SPI：
 * 原理图上 CS/SCK/SI 接在 PA4/PA5/PA6，而 SPI1 的默认映射是
 * SCK=PA5 / MISO=PA6 / MOSI=PA7。SI 落在 PA6（MISO 脚）上，
 * 硬件 SPI 发不出数据，所以这里用 GPIO 翻转时序。
 *
 * 如果改板把它挪到真正的 MOSI 上，把 mcp_xfer() 换成
 * HAL_SPI_Transmit(&hspi1, buf, 2, 10) 即可，其余不变。
 *
 * 时序要点（datasheet Figure 6-1）：
 *   1. CS 拉低；
 *   2. 两个字节共 16 个 SCK 上升沿，MSB first；
 *   3. CS 拉高 —— 帧在此刻才生效。
 * 少一个字节、或者中途抬 CS，整帧作废，现象是"不报错但音量不动"。
 */
#include "drv_mcp41010.h"

#include <stddef.h>

#include "audio_core.h"
#include "board_pins.h"

static uint8_t s_code = 0x80u;   /* 影子变量，上电默认中点 */

/* MCP41010 的 SCK 上限 10MHz，软件翻转远低于此，
 * 但面包板飞线仍然不宜太快，加几个空转保证建立时间。 */
static void bit_delay(void)
{
    volatile uint8_t i;
    for (i = 0u; i < 8u; i++) {
        __NOP();
    }
}

static void mcp_xfer(uint8_t cmd, uint8_t data)
{
    uint8_t buf[2];
    uint8_t i, bit;

    buf[0] = cmd;
    buf[1] = data;

    HAL_GPIO_WritePin(DPOT_SCK_PORT, DPOT_SCK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DPOT_CS_PORT,  DPOT_CS_PIN,  GPIO_PIN_RESET);
    bit_delay();

    for (i = 0u; i < 2u; i++) {
        for (bit = 0u; bit < 8u; bit++) {
            uint8_t b = (uint8_t)((buf[i] & 0x80u) != 0u);

            HAL_GPIO_WritePin(DPOT_SI_PORT, DPOT_SI_PIN,
                              b ? GPIO_PIN_SET : GPIO_PIN_RESET);
            bit_delay();
            HAL_GPIO_WritePin(DPOT_SCK_PORT, DPOT_SCK_PIN, GPIO_PIN_SET);
            bit_delay();
            HAL_GPIO_WritePin(DPOT_SCK_PORT, DPOT_SCK_PIN, GPIO_PIN_RESET);
            buf[i] = (uint8_t)(buf[i] << 1);    /* 低位先左移，最后清零无妨 */
        }
    }

    HAL_GPIO_WritePin(DPOT_CS_PORT, DPOT_CS_PIN, GPIO_PIN_SET);
    bit_delay();
}

void mcp41010_init(void)
{
    HAL_GPIO_WritePin(DPOT_CS_PORT,  DPOT_CS_PIN,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(DPOT_SCK_PORT, DPOT_SCK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(DPOT_SI_PORT,  DPOT_SI_PIN,  GPIO_PIN_RESET);

    mcp41010_write_code(0x00u);     /* 最小增益起步，避免上电爆音 */
}

void mcp41010_write_code(uint8_t code)
{
    mcp_xfer(MCP41010_CMD_WRITE, code);
    s_code = code;
}

void mcp41010_set_level(uint8_t level)
{
    mcp41010_write_code(volume_level_to_code(level));
}

uint8_t mcp41010_get_code(void)
{
    return s_code;
}

void mcp41010_shutdown(int on)
{
    uint8_t cmd = on ? MCP41010_CMD_SHUTDOWN : MCP41010_CMD_WRITE;

    mcp_xfer(cmd, s_code);
}
