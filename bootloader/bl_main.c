/*
 * BMS CAN Bootloader 主决策:时钟 + LED + 校验 App / 跳转 / 进 OTA。
 * 决策:BKP 魔数? → OTA;否则 App 头 magic+硬件CRC32 有效 → 跳 App;都不满足 → OTA。
 */
#include "bl.h"

#define RCC_BASE    0x40021000U
#define RCC_CR      REG(RCC_BASE + 0x00)
#define RCC_CFGR    REG(RCC_BASE + 0x04)
#define RCC_AHBENR  REG(RCC_BASE + 0x14)
#define RCC_APB2ENR REG(RCC_BASE + 0x18)
#define RCC_APB1ENR REG(RCC_BASE + 0x1C)
#define FLASH_ACR   REG(0x40022000U)
#define GPIOA_CRH   REG(0x40010800U + 0x04)
#define GPIOA_BSRR  REG(0x40010800U + 0x10)
#define AFIO_MAPR   REG(0x40010000U + 0x04)
#define CRC_DR      REG(0x40023000U + 0x00)
#define CRC_CR      REG(0x40023000U + 0x08)
#define PWR_CR      REG(0x40007000U + 0x00)
#define BKP_DR1     REG(0x40006C00U + 0x04)
#define SCB_VTOR    REG(0xE000ED08U)

static inline void set_msp(uint32_t sp) { __asm volatile("msr msp, %0" ::"r"(sp)); }
static inline void disable_irq(void) { __asm volatile("cpsid i" ::: "memory"); }

static void clock_init_72mhz(void)
{
    RCC_CR |= (1U << 16);                 /* HSEON */
    while ((RCC_CR & (1U << 17)) == 0U) {
    }
    FLASH_ACR = (1U << 4) | 0x2U;         /* PRFTBE + LATENCY=2 */
    RCC_CFGR = (1U << 16) | (7U << 18) | (4U << 8); /* PLL=HSEx9, APB1/2 */
    RCC_CR |= (1U << 24);                 /* PLLON */
    while ((RCC_CR & (1U << 25)) == 0U) {
    }
    RCC_CFGR |= (2U << 0);                /* SW=PLL */
    while ((RCC_CFGR & (3U << 2)) != (2U << 2)) {
    }
}

static void led_init(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 0);               /* IOPAEN + AFIOEN */
    AFIO_MAPR = (AFIO_MAPR & ~(7U << 24)) | (2U << 24); /* SWJ_CFG=010: JTAG off, SWD on */
    GPIOA_CRH = (GPIOA_CRH & ~(0xFU << 28)) | (2U << 28); /* PA15 输出推挽 2MHz */
}

void led_set(int on)
{
    GPIOA_BSRR = on ? (1U << (15 + 16)) : (1U << 15); /* 低有效 */
}

uint32_t hw_crc32(const uint32_t *data, uint32_t words)
{
    RCC_AHBENR |= (1U << 6);
    CRC_CR = 1U;
    for (uint32_t i = 0; i < words; ++i) {
        CRC_DR = data[i];
    }
    return CRC_DR;
}

static int app_valid(void)
{
    const app_header_t *hdr = (const app_header_t *)APP_HDR_ADDR;

    if (hdr->magic != APP_HDR_MAGIC) {
        return 0;
    }
    if ((hdr->length == 0U) || (hdr->length & 3U) || (hdr->length > APP_MAX_LEN)) {
        return 0;
    }
    return hw_crc32((const uint32_t *)APP_BASE_ADDR, hdr->length / 4U) == hdr->crc32;
}

static void jump_to_app(void)
{
    uint32_t sp = *(volatile uint32_t *)APP_BASE_ADDR;
    uint32_t pc = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

    disable_irq();
    SCB_VTOR = APP_BASE_ADDR;
    set_msp(sp);
    ((void (*)(void))pc)();
}

static int ota_requested(void)
{
    RCC_APB1ENR |= (1U << 28) | (1U << 27); /* PWREN + BKPEN */
    PWR_CR |= (1U << 8);                    /* DBP */
    if ((BKP_DR1 & 0xFFFFU) == BKP_OTA_MAGIC) {
        BKP_DR1 = 0U;
        return 1;
    }
    return 0;
}

int bl_main(void)
{
    clock_init_72mhz();
    led_init();

    if (ota_requested() || !app_valid()) {
        ota_run(); /* 不返回 */
    }
    jump_to_app(); /* 不返回 */
    return 0;
}
