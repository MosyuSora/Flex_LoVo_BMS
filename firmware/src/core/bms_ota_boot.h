#ifndef BMS_OTA_BOOT_H
#define BMS_OTA_BOOT_H

#include <stdbool.h>

/*
 * 置"进 OTA"标志到 BKP 备份寄存器(软复位不丢),供 Bootloader 复位后识别。
 * 写完回读确认。返回 true = 已置且确认无误(调用方据此决定是否复位)。
 * 魔数 0x4F54 必须与 bootloader/bl.h 的 BKP_OTA_MAGIC 一致。
 */
bool bms_ota_boot_arm(void);

#endif /* BMS_OTA_BOOT_H */
