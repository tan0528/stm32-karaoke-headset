/*
 * drv_mcp41010.h -- MCP41010 单通道 8 位数字电位器
 *
 * 只能写、不能回读：芯片没有 MISO，所以状态必须由 MCU 侧影子变量维护。
 * 上电后抽头默认在中点（128 / 50%），必须主动写一次。
 */
#ifndef DRV_MCP41010_H
#define DRV_MCP41010_H

#include <stdint.h>

#define MCP41010_CMD_WRITE     0x11u   /* C1C0 = 01：写数据 */
#define MCP41010_CMD_SHUTDOWN  0x21u   /* C1C0 = 10：A/W/B 高阻 */

/* 把 GPIO 拉成空闲态，并把抽头置到 0（起步静音，避免上电爆音） */
void    mcp41010_init(void);

/* 直接写抽头码 0..255 */
void    mcp41010_write_code(uint8_t code);

/* 按人机档位 0..100 设置（内部做三次方映射与方向反相） */
void    mcp41010_set_level(uint8_t level);

/* 影子变量：最后一次成功写入的抽头码 */
uint8_t mcp41010_get_code(void);

/* 进入 / 退出高阻关断 */
void    mcp41010_shutdown(int on);

#endif /* DRV_MCP41010_H */
