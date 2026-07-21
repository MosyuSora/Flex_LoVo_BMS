/* BMS CAN Bootloader —— 共享定义(寄存器宏、flash 布局、CAN 协议、App 头)。 */
#ifndef BL_H
#define BL_H

#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* ---- flash 布局 ---- */
#define FLASH_ORIGIN   0x08000000U
#define FLASH_SIZE     0x10000U    /* 64KB */
#define FLASH_PAGE     0x400U      /* 1KB 页 */
#define APP_HDR_ADDR   0x08001000U /* App 头(1 页,commit 标记) */
#define APP_BASE_ADDR  0x08001400U /* App 镜像/向量表 */
#define APP_MAX_LEN    (FLASH_ORIGIN + FLASH_SIZE - APP_BASE_ADDR) /* 上限字节 */
#define APP_HDR_MAGIC  0x4F544201U /* "OTA\x01" */
#define BKP_OTA_MAGIC  0x4F54U     /* "OT":App 请求进 OTA */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t length;  /* 4 对齐 */
    uint32_t crc32;
} app_header_t;

/* ---- CAN OTA 协议(扩展帧,避开遥测 0x0001-7 / 命令 0x0100) ---- */
#define OTA_ID_START   0x0700U /* host->BL: length(4,BE)+crc32(4,BE) */
#define OTA_ID_DATA    0x0701U /* host->BL: 8B 顺序数据 */
#define OTA_ID_END     0x0702U /* host->BL: version(4,BE) */
#define OTA_ID_STATUS  0x0708U /* BL->host: code(1)+page(2,BE) */

enum {
    OTA_ST_READY = 0,
    OTA_ST_PAGE_OK = 1,
    OTA_ST_PAGE_ERR = 2,
    OTA_ST_DONE = 3,
    OTA_ST_CRC_FAIL = 4,
    OTA_ST_ERR = 5,
};

/* ---- 一帧 CAN(扩展) ---- */
typedef struct {
    uint32_t id;   /* 扩展 ID */
    uint8_t dlc;
    uint8_t data[8];
} can_frame_t;

/* ---- 硬件 CRC32(init 0xFFFFFFFF, poly 0x04C11DB7, 无反转/末异或);words=字数 ---- */
uint32_t hw_crc32(const uint32_t *data, uint32_t words);

/* ---- flash 驱动 ---- */
void flash_unlock(void);
void flash_lock(void);
int flash_erase_page(uint32_t addr);            /* 0=ok */
int flash_write(uint32_t addr, const uint8_t *data, uint32_t len); /* 半字编程,len 需偶数 */

/* ---- bxCAN 500k ---- */
void can_init(void);
int can_rx(can_frame_t *f);                     /* 1=收到 0=空 */
void can_tx(uint32_t id, const uint8_t *data, uint8_t dlc);

/* ---- LED(PA15/D3,低有效) ---- */
void led_set(int on);

/* ---- OTA 主循环(不返回:成功后复位,失败留在这) ---- */
void ota_run(void);

#endif /* BL_H */
