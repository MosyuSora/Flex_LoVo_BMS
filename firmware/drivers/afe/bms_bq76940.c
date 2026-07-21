/*
 * BQ76940 设备驱动(9-15 串,I2C)。本板实操核心。
 *
 * 它"是"一颗 BMS AFE(继承 bms_afe_driver_api),实现全在家族核
 * bq769x0_core.c;这里只做一件事——把本 compatible 的 DT 节点参数实例化
 * 并注册。三个 BQ769x0 设备驱动的区别只有 DT_DRV_COMPAT 这一行。
 */
#define DT_DRV_COMPAT bms_bq76940

#include "bq769x0_core.h"

DT_INST_FOREACH_STATUS_OKAY(BQ769X0_DEVICE_DEFINE)
