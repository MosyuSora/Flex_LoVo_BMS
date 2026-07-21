/*
 * OTA 协议状态机(替掉骨架里的占位闪灯)。
 * START(长度+CRC)→ 擦区回 READY → DATA 累到 1KB 写一页回 PAGE_OK → END 全镜像 CRC 校验
 * → 过则写头 commit + 复位跳 App,不过回 CRC_FAIL 留在 OTA。全程 D3 10Hz 快闪。
 */
#include "bl.h"

#define STK_CTRL REG(0xE000E010U)
#define STK_LOAD REG(0xE000E014U)
#define STK_VAL  REG(0xE000E018U)
#define SCB_AIRCR REG(0xE000ED0CU)
#define IWDG_KR  REG(0x40003000U) /* 独立看门狗键寄存器 */

/*
 * 喂狗:App 起过 IWDG(2s),软复位不停它——复位后它还在跑,Bootloader 不喂就会被它
 * 2s 复位、OTA 永远做不完。喂一个没启的 IWDG 也无害(键寄存器),所以无脑喂。
 */
static void iwdg_feed(void) { IWDG_KR = 0xAAAAU; }

static uint8_t s_page[FLASH_PAGE]; /* 一页缓冲(1KB) */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void status(uint8_t code, uint16_t page)
{
    uint8_t d[8] = {code, (uint8_t)(page >> 8), (uint8_t)page, 0, 0, 0, 0, 0};
    can_tx(OTA_ID_STATUS, d, 3);
}

static void system_reset(void)
{
    SCB_AIRCR = 0x05FA0004U; /* VECTKEY | SYSRESETREQ */
    for (;;) {
    }
}

/*
 * 把攒满的一页写进 flash 并回 PAGE_OK,写完清空填充计数。失败回 PAGE_ERR。
 * 抽成函数是为了让 DATA 收帧循环里"满一页就立刻落盘"、把 s_page 访问严格夹在
 * [0, FLASH_PAGE) 内——短帧导致的错位不会再越过 == 判定而写爆缓冲。返回 0=ok。
 */
static int flush_page(uint32_t *write_addr, uint32_t *page_fill, uint16_t *page_no)
{
    uint32_t n = *page_fill;

    if (flash_write(*write_addr, s_page, (n + 1U) & ~1U) != 0) {
        status(OTA_ST_PAGE_ERR, *page_no);
        return -1;
    }
    *write_addr += n;
    *page_fill = 0;
    status(OTA_ST_PAGE_OK, *page_no);
    (*page_no)++;
    return 0;
}

/* 10Hz 闪灯:SysTick 50ms COUNTFLAG,轮询里翻灯,不用中断 */
static void blink_init(void)
{
    STK_LOAD = (72000000U / 20U) - 1U; /* 50ms @72MHz */
    STK_VAL = 0U;
    STK_CTRL = (1U << 0) | (1U << 2);  /* ENABLE + CLKSOURCE=core,不开中断 */
}

static void blink_poll(void)
{
    static int on = 0;

    if (STK_CTRL & (1U << 16)) { /* COUNTFLAG:一个 50ms 到 */
        on = !on;
        led_set(on);
    }
}

void ota_run(void)
{
    uint32_t exp_len = 0, exp_crc = 0;
    uint32_t recv = 0;      /* 已收字节 */
    uint32_t page_fill = 0; /* 当前页缓冲已填 */
    uint32_t write_addr = APP_BASE_ADDR;
    uint16_t page_no = 0;
    int armed = 0;          /* 是否收到过合法 START */

    blink_init();
    can_init();
    led_set(1);

    for (;;) {
        can_frame_t f;

        iwdg_feed();
        blink_poll();
        if (!can_rx(&f)) {
            continue;
        }

        if (f.id == OTA_ID_START) {
            exp_len = be32(&f.data[0]);
            exp_crc = be32(&f.data[4]);
            if ((exp_len == 0U) || (exp_len & 3U) || (exp_len > APP_MAX_LEN)) {
                status(OTA_ST_ERR, 0);
                armed = 0;
                continue;
            }
            /* 擦:先擦头页(立刻使旧 App 失效),再擦够放镜像的 App 页 */
            flash_unlock();
            int err = flash_erase_page(APP_HDR_ADDR);
            uint32_t pages = (exp_len + FLASH_PAGE - 1U) / FLASH_PAGE;
            for (uint32_t i = 0; (i < pages) && (err == 0); ++i) {
                iwdg_feed(); /* 擦多页可能逼近 2s,擦一页喂一次 */
                err = flash_erase_page(APP_BASE_ADDR + i * FLASH_PAGE);
            }
            if (err != 0) {
                status(OTA_ST_ERR, 0);
                armed = 0;
                continue;
            }
            recv = 0; page_fill = 0; write_addr = APP_BASE_ADDR; page_no = 0;
            armed = 1;
            status(OTA_ST_READY, 0);
        } else if ((f.id == OTA_ID_DATA) && armed) {
            uint32_t n = f.dlc;
            if (n > 8U) { n = 8U; }
            if (recv + n > exp_len) { n = exp_len - recv; } /* 不越过总长 */
            /* 逐字节入缓冲,满一页/到末尾立刻落盘:page_fill 始终 < FLASH_PAGE,
             * 短帧错位也写不爆 s_page(致命越界防线)。 */
            for (uint32_t i = 0; i < n; ++i) {
                s_page[page_fill++] = f.data[i];
                recv++;
                if ((page_fill != FLASH_PAGE) && (recv != exp_len)) {
                    continue; /* 未满页且未到末尾,继续攒 */
                }
                if (flush_page(&write_addr, &page_fill, &page_no) != 0) {
                    armed = 0;
                    break;
                }
            }
        } else if ((f.id == OTA_ID_END) && armed) {
            uint32_t ver = be32(&f.data[0]);
            uint32_t crc = hw_crc32((const uint32_t *)APP_BASE_ADDR, exp_len / 4U);

            if ((recv == exp_len) && (crc == exp_crc)) {
                app_header_t hdr = {APP_HDR_MAGIC, ver, exp_len, exp_crc};
                if (flash_write(APP_HDR_ADDR, (const uint8_t *)&hdr, sizeof(hdr)) == 0) {
                    flash_lock();
                    status(OTA_ST_DONE, 0);
                    for (volatile uint32_t d = 0; d < 2000000U; ++d) { } /* 让 STATUS 发出去 */
                    system_reset(); /* 复位 → 校验 App 有效 → 跳 App */
                }
            }
            status(OTA_ST_CRC_FAIL, 0);
            armed = 0; /* 等重传 */
        }
    }
}
