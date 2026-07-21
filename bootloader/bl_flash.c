/* STM32F103 内部 flash 编程(FPEC 寄存器级):解锁 / 擦 1KB 页 / 半字写。 */
#include "bl.h"

#define FLASH_BASE 0x40022000U
#define FLASH_KEYR REG(FLASH_BASE + 0x04)
#define FLASH_SR   REG(FLASH_BASE + 0x0C)
#define FLASH_CR   REG(FLASH_BASE + 0x10)
#define FLASH_AR   REG(FLASH_BASE + 0x14)

#define SR_BSY   (1U << 0)
#define SR_PGERR (1U << 2)
#define SR_WRPRT (1U << 4)
#define SR_EOP   (1U << 5)

#define CR_PG   (1U << 0)
#define CR_PER  (1U << 1)
#define CR_STRT (1U << 6)
#define CR_LOCK (1U << 7)

static void flash_wait(void)
{
    while (FLASH_SR & SR_BSY) {
    }
    FLASH_SR = SR_EOP | SR_PGERR | SR_WRPRT; /* 写 1 清标志 */
}

void flash_unlock(void)
{
    if (FLASH_CR & CR_LOCK) {
        FLASH_KEYR = 0x45670123U;
        FLASH_KEYR = 0xCDEF89ABU;
    }
}

void flash_lock(void)
{
    FLASH_CR |= CR_LOCK;
}

int flash_erase_page(uint32_t addr)
{
    flash_wait();
    FLASH_CR |= CR_PER;
    FLASH_AR = addr;
    FLASH_CR |= CR_STRT;
    flash_wait();
    FLASH_CR &= ~CR_PER;
    /* 校验:擦干净应为全 0xFF */
    for (uint32_t i = 0; i < FLASH_PAGE; i += 4U) {
        if (REG(addr + i) != 0xFFFFFFFFU) {
            return -1;
        }
    }
    return 0;
}

int flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if ((addr & 1U) || (len & 1U)) {
        return -1; /* 半字对齐 */
    }
    for (uint32_t i = 0; i < len; i += 2U) {
        uint16_t hw = (uint16_t)(data[i] | ((uint16_t)data[i + 1U] << 8));

        FLASH_CR |= CR_PG;
        *(volatile uint16_t *)(addr + i) = hw;
        flash_wait();
        FLASH_CR &= ~CR_PG;
        if (*(volatile uint16_t *)(addr + i) != hw) {
            return -1; /* 回读校验失败 */
        }
    }
    return 0;
}
