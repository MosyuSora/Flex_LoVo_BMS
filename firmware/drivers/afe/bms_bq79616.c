/*
 * ============================================================================
 * BQ79616-Q1 设备驱动骨架(车规级,最高 16 串,隔离菊链)
 * ============================================================================
 *
 * 状态:骨架完整,菊链协议体【未写完】(占位)。这是"理论扩展/未接板"型号
 * 当前实现仍是待完善骨架,放在这里的目的是**证明同一个 IBmsAfe
 * 总线接口可以换一种完全不同的传输**:
 *   - BQ76940 / BQ76930:走 I2C,家族核 bq769x0_core.c。
 *   - BQ79616-Q1:走**隔离菊链(差分 UART,COMML/COMMH)**,不碰 I2C。
 * 两者继承同一张 bms_afe_driver_api,上层(SampleTask 等)拿到的都是
 * const struct device *,一行不改。这正是"接口跟传输方式无关"。
 *
 * 编译:整份驱动在 devicetree 出现 status=okay 的 bms,bq79616 节点时才编入
 * (当前板卡未接此芯片,DT_INST_FOREACH 展开为空)。加真实节点即激活并做
 * 语法/链接检查。
 *
 * 【要补的地方,均以 "TODO(bq79616)" 标注】:
 *   1) 菊链帧编解码:帧头/命令/设备地址/寄存器地址/长度/数据/CRC16。
 *   2) 自动编址(auto-addressing):上电给链上每颗器件分配地址。
 *   3) UART 收发 + 超时 + 差分总线方向控制。
 *   4) 各寄存器地址与换算系数(BQ79616 与 BQ769x0 寄存器映射不同)。
 */
#define DT_DRV_COMPAT bms_bq79616

#include "afe/bms_afe.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* 每个实例的板级事实(const,来自 devicetree) */
struct bq79616_config {
	const struct device *uart; /* 菊链差分 UART(base 侧收发口) */
	const uint8_t *cell_channel_map;
	uint8_t cell_count;
	uint8_t cell_channel_map_len;
	uint8_t temp_count;
	uint8_t chain_position; /* 本器件在菊链上的位置/地址 */
};

/* 每个实例的可变状态 */
struct bq79616_data {
	struct k_mutex lock;
	bool addressed; /* 是否已完成自动编址 */
};

/* ---- 菊链传输原语(【未写完】,仅留签名与骨架) ---- */

/* TODO(bq79616): 计算 BQ79616 菊链帧的 CRC16(TI 规定的多项式/字节序)。 */
static uint16_t bq79616_crc16(const uint8_t *frame, size_t len)
{
	ARG_UNUSED(frame);
	ARG_UNUSED(len);
	return 0U; /* 占位 */
}

/*
 * TODO(bq79616): 组一帧"读寄存器"请求并通过 UART 发出,再收回应答帧、校验
 * CRC16、取出数据。方向控制、超时、单播/广播都在这里处理。
 */
static int bq79616_read_regs(const struct device *dev, uint16_t reg,
			     uint8_t *buf, size_t len)
{
	const struct bq79616_config *config = dev->config;

	ARG_UNUSED(reg);
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	(void)bq79616_crc16(NULL, 0);

	if (!device_is_ready(config->uart)) {
		return -ENODEV;
	}
	/* TODO(bq79616): 构帧 -> uart 发 -> 收应答 -> 校验 -> 拷数据。 */
	return -ENOSYS; /* 未写完 */
}

/*
 * TODO(bq79616): 组一帧"写寄存器"请求(带 CRC16)并发出。
 */
static int bq79616_write_regs(const struct device *dev, uint16_t reg,
			      const uint8_t *buf, size_t len)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(reg);
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	return -ENOSYS; /* 未写完 */
}

/* ---- bms_afe_driver_api 实现(骨架:结构齐全,取数逻辑未写完) ---- */

static int bq79616_init(const struct device *dev)
{
	const struct bq79616_config *config = dev->config;
	struct bq79616_data *data = dev->data;

	if (!device_is_ready(config->uart)) {
		return -ENODEV;
	}
	/*
	 * TODO(bq79616): 1) 唤醒菊链;2) 自动编址,给链上每颗器件分配地址;
	 * 3) 配置 ADC / 采样通道 / 保护阈值。完成后置 data->addressed = true。
	 */
	data->addressed = false;
	return -ENOSYS; /* 未写完 */
}

static int bq79616_read_cells(const struct device *dev, uint16_t *cell_mv, size_t cell_count)
{
	const struct bq79616_config *config = dev->config;

	if ((cell_mv == NULL) || (cell_count > config->cell_count)) {
		return -EINVAL;
	}
	/*
	 * TODO(bq79616): 读 VC 寄存器组,按 cell_channel_map 映射到逻辑电芯,
	 * 用 BQ79616 的 LSB 换算成 mV。
	 */
	return bq79616_read_regs(dev, /*VCELL base*/ 0x0000U,
				 (uint8_t *)cell_mv, cell_count * 2U);
}

static int bq79616_read_pack_voltage(const struct device *dev, uint16_t *pack_mv)
{
	ARG_UNUSED(dev);
	if (pack_mv == NULL) {
		return -EINVAL;
	}
	return -ENOSYS; /* TODO(bq79616): 读 BATT/堆叠电压寄存器或电芯求和 */
}

static int bq79616_read_current(const struct device *dev, int16_t *current_ma)
{
	ARG_UNUSED(dev);
	if (current_ma == NULL) {
		return -EINVAL;
	}
	return -ENOSYS; /* TODO(bq79616): 读库仑计/分流电流寄存器并换算 */
}

static int bq79616_read_thermistor_mv(const struct device *dev, uint16_t *ts_mv,
				      size_t count)
{
	const struct bq79616_config *config = dev->config;

	if ((ts_mv == NULL) || (count > config->temp_count)) {
		return -EINVAL;
	}
	/* TODO(bq79616): 读 GPIO/AUX 温度通道分压 mV(曲线换算交给 bms_ntc)。 */
	return -ENOSYS;
}

static int bq79616_read_fault_status(const struct device *dev, uint16_t *fault_bits)
{
	ARG_UNUSED(dev);
	if (fault_bits == NULL) {
		return -EINVAL;
	}
	return -ENOSYS; /* TODO(bq79616): 读 FAULT_SUMMARY 等状态寄存器 */
}

static int bq79616_clear_fault_status(const struct device *dev, uint16_t fault_bits)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(fault_bits);
	return -ENOSYS; /* TODO(bq79616): 写 FAULT_RST 清除对应位 */
}

static int bq79616_set_balance_mask(const struct device *dev, uint16_t cell_mask)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cell_mask);
	return -ENOSYS; /* TODO(bq79616): 写 CB_CELL 寄存器组开均衡 */
}

static int bq79616_set_chg_enabled(const struct device *dev, bool enabled)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(enabled);
	/*
	 * 注:车规监测器通常不直接驱动主回路 FET(由外部接触器/预驱负责)。
	 * TODO(bq79616): 若板级用 BQ79616 的 GPIO 控外部驱动,则在此实现;
	 * 否则保持 -ENOSYS,由上层选择别的执行器。
	 */
	return -ENOSYS;
}

static int bq79616_set_dsg_enabled(const struct device *dev, bool enabled)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(enabled);
	return -ENOSYS; /* 同 set_chg_enabled 的说明 */
}

static int bq79616_read_switch_state(const struct device *dev, BmsSwitchState *out)
{
	ARG_UNUSED(dev);
	if (out == NULL) {
		return -EINVAL;
	}
	out->chg_enabled = false;
	out->dsg_enabled = false;
	return -ENOSYS; /* TODO(bq79616): 读 GPIO 状态回读 FET 使能 */
}

static int bq79616_device_init(const struct device *dev)
{
	struct bq79616_data *data = dev->data;

	data->addressed = false;
	return k_mutex_init(&data->lock);
}

/* 与 BQ769x0 家族填的是同一张 bms_afe_driver_api —— 接口复用的证据。 */
static const struct bms_afe_driver_api bq79616_afe_api = {
	.init = bq79616_init,
	.measure = {
		.read_cells = bq79616_read_cells,
		.read_pack_voltage = bq79616_read_pack_voltage,
		.read_current = bq79616_read_current,
		.read_thermistor_mv = bq79616_read_thermistor_mv,
	},
	.protect = {
		.read_fault_status = bq79616_read_fault_status,
		.clear_fault_status = bq79616_clear_fault_status,
	},
	.balance = {
		.set_balance_mask = bq79616_set_balance_mask,
	},
	.power = {
		.set_chg_enabled = bq79616_set_chg_enabled,
		.set_dsg_enabled = bq79616_set_dsg_enabled,
		.read_switch_state = bq79616_read_switch_state,
	},
};

#define BQ79616_DEVICE_DEFINE(inst)                                                \
	static const uint8_t bq79616_cell_map_##inst[] =                           \
		DT_INST_PROP(inst, cell_channels);                                 \
	static struct bq79616_data bq79616_data_##inst;                            \
	static const struct bq79616_config bq79616_config_##inst = {               \
		.uart = DEVICE_DT_GET(DT_INST_PHANDLE(inst, uart)),                \
		.cell_channel_map = bq79616_cell_map_##inst,                       \
		.cell_count = DT_INST_PROP(inst, cell_count),                      \
		.cell_channel_map_len = DT_INST_PROP_LEN(inst, cell_channels),     \
		.temp_count = DT_INST_PROP(inst, temp_count),                      \
		.chain_position = DT_INST_PROP_OR(inst, chain_position, 1),        \
	};                                                                         \
	DEVICE_DT_INST_DEFINE(inst,                                                \
			      bq79616_device_init,                                 \
			      NULL,                                                \
			      &bq79616_data_##inst,                                \
			      &bq79616_config_##inst,                              \
			      POST_KERNEL,                                         \
			      CONFIG_SERIAL_INIT_PRIORITY,                         \
			      &bq79616_afe_api);

DT_INST_FOREACH_STATUS_OKAY(BQ79616_DEVICE_DEFINE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(bms_bq79616) */
