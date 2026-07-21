#ifndef BQ769X0_CORE_H
#define BQ769X0_CORE_H

#include "afe/bms_afe.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

/*
 * ============================================================================
 * BQ769x0 家族核(设备驱动共享实现)
 * ============================================================================
 *
 * BQ76920 / BQ76930 / BQ76940 是 TI BQ769x0 同一家族:寄存器切面、CRC 写帧、
 * ADC/NTC 换算完全一致,只在**有效电芯数 / 热敏通道数 / 物理槽位**上不同——
 * 而这些差异全部来自 devicetree(cell-count / temp-count / cell-channels)。
 *
 * 所以三颗芯片共享这一份核实现(填同一张 bms_afe_driver_api),各自的设备驱动
 * 文件(bms_bq76940.c / bms_bq76930.c)只做一件事:用 BQ769X0_DEVICE_DEFINE
 * 宏把自己 DT 节点的参数实例化 + 注册。这正是 Linux 里"家族核 + 每型号注册"
 * 的写法。
 */

#define BQ769X0_MAX_CELLS 15U
#define BQ769X0_MAX_TS_CHANNELS 3U /* TS1/TS2/TS3,覆盖 20/30/40 全家族 */

/* 每个 I2C 版 BQ769x0 实例的板级事实(const,来自 devicetree) */
struct bq769x0_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec wake_gpio;
	uint32_t current_lsb_ua;
	const uint8_t *cell_channel_map; /* 逻辑电芯 -> 物理 VC 槽位(1..15) */
	uint8_t cell_count;
	uint8_t cell_channel_map_len;
	uint8_t temp_count;
};

/* 每个实例的可变状态 */
struct bq769x0_data {
	struct k_mutex lock;
	uint16_t adc_gain_uv;
	int8_t adc_offset_mv;
	uint8_t boot_sys_stat; /* 上电时读到的原始状态,供诊断 */
};

/* device init level 的构造(只初始化锁);芯片唤醒/配置在 api->init。 */
int bq769x0_device_init(const struct device *dev);

/* 家族共享的 AFE 虚表:三个设备驱动都注册它。 */
extern const struct bms_afe_driver_api bq769x0_afe_api;

/*
 * 设备驱动展开宏:在具体驱动 .c 里(已 #define DT_DRV_COMPAT)配合
 * DT_INST_FOREACH_STATUS_OKAY 使用。它把 DT 节点的参数实例化成 config/data
 * 并注册,BUILD_ASSERT 编译期校验 cell-channels 长度等于 cell-count。
 */
#define BQ769X0_DEVICE_DEFINE(inst)                                                \
	BUILD_ASSERT(DT_INST_PROP_LEN(inst, cell_channels) ==                      \
			     DT_INST_PROP(inst, cell_count),                       \
		     "BQ769x0 cell-channels length must equal cell-count");        \
	static const uint8_t bq769x0_cell_map_##inst[] =                           \
		DT_INST_PROP(inst, cell_channels);                                 \
	static struct bq769x0_data bq769x0_data_##inst;                            \
	static const struct bq769x0_config bq769x0_config_##inst = {               \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                 \
		.wake_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, wake_gpios, {0}),       \
		.current_lsb_ua = DT_INST_PROP(inst, current_lsb_ua),              \
		.cell_channel_map = bq769x0_cell_map_##inst,                       \
		.cell_count = DT_INST_PROP(inst, cell_count),                      \
		.cell_channel_map_len = DT_INST_PROP_LEN(inst, cell_channels),     \
		.temp_count = DT_INST_PROP(inst, temp_count),                      \
	};                                                                         \
	DEVICE_DT_INST_DEFINE(inst,                                                \
			      bq769x0_device_init,                                 \
			      NULL,                                                \
			      &bq769x0_data_##inst,                                \
			      &bq769x0_config_##inst,                              \
			      POST_KERNEL,                                         \
			      CONFIG_I2C_INIT_PRIORITY,                            \
			      &bq769x0_afe_api);

#endif
