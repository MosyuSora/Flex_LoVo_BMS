/*
 * BQ76930 设备驱动(6-10 串,I2C)。BQ769x0 家族同系列低串数型号。
 *
 * 与 BQ76940 寄存器切面、CRC、换算完全一致,只是有效电芯/热敏通道更少——
 * 差异全部来自 devicetree(cell-count / cell-channels / temp-count),所以
 * 直接复用家族核,本文件只换 compatible。当前板卡未接 BQ76930,无 okay 节点
 * 时 DT_INST_FOREACH 展开为空,不产生实例;换板加节点即生效。
 */
#define DT_DRV_COMPAT bms_bq76930

#include "bq769x0_core.h"

DT_INST_FOREACH_STATUS_OKAY(BQ769X0_DEVICE_DEFINE)
