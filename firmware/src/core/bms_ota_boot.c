#include "core/bms_ota_boot.h"

#include <stdint.h>

/* STM32F103 裸寄存器(BKP 在备份域,需先开 PWR/BKP 时钟 + DBP 才能写) */
#define REG(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define RCC_APB1ENR REG(0x4002101CU)
#define PWR_CR      REG(0x40007000U)
#define BKP_DR1     REG(0x40006C04U)
#define BKP_OTA_MAGIC 0x4F54U

bool bms_ota_boot_arm(void)
{
	RCC_APB1ENR |= (1U << 28) | (1U << 27); /* PWREN + BKPEN */
	PWR_CR |= (1U << 8);                    /* DBP:允许写备份域 */
	BKP_DR1 = BKP_OTA_MAGIC;
	return (BKP_DR1 & 0xFFFFU) == BKP_OTA_MAGIC; /* 回读确认写进去了 */
}
