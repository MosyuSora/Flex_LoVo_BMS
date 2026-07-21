#include "bq769x0_core.h"

#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bq769x0, LOG_LEVEL_INF);

/*
 * BQ769x0 family core. Register layout follows
 * docs/board-facts/bq76940_config.json.
 * 对齐:
 * - SYS_STAT   故障状态,写 1 清除(粘滞位)。
 * - CELLBAL1-3 被动均衡,每 5 节一组。
 * - SYS_CTRL2  bit0/bit1 = CHG/DSG FET,bit6 = 库仑计使能。
 *
 * 传输契约:写寄存器带 CRC(crc8 poly 0x07,覆盖 [写地址字节, reg, value]);
 * 读逐字节(vendor 位翻转 I2C 与 Renode 外设模型都是单字节窗口)。
 * 并发契约:所有 op 由 data->lock 整体互斥,多寄存器序列在锁内是原子。
 */
#define REG_SYS_STAT 0x00U
/* SYS_STAT bit7 = CC_READY:库仑计新读数就绪标志(非故障,写 1 清除) */
#define SYS_STAT_CC_READY 0x80U
#define REG_CELLBAL1 0x01U
#define REG_CELLBAL2 0x02U
#define REG_CELLBAL3 0x03U
#define REG_SYS_CTRL1 0x04U
#define REG_SYS_CTRL2 0x05U
#define REG_CC_CFG 0x0BU
#define REG_VC1_HI 0x0CU
#define REG_TS1_HI 0x2CU
#define REG_TS2_HI 0x2EU
#define REG_TS3_HI 0x30U
#define REG_CC_HI 0x32U
#define REG_ADCGAIN1 0x50U
#define REG_ADCOFFSET 0x51U
#define REG_ADCGAIN2 0x59U

#define SYS_CTRL1_TEMP_SEL BIT(3)
#define SYS_CTRL1_ADC_EN BIT(4)
#define SYS_CTRL2_CHG_ON BIT(0)
#define SYS_CTRL2_DSG_ON BIT(1)
#define SYS_CTRL2_CC_EN BIT(6)
#define CC_CFG_DEFAULT 0x19U /* datasheet 要求的固定值 */

#define ADC_GAIN_BASE_UV 365U
#define CRC_POLY 0x07U

/* ---- 传输原语(调用方必须已持锁) ---- */

static uint8_t crc8(const uint8_t *data, size_t length)
{
	uint8_t crc = 0U;

	for (size_t i = 0U; i < length; ++i) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit) {
			if ((crc & 0x80U) != 0U) {
				crc = (uint8_t)((crc << 1) ^ CRC_POLY);
			} else {
				crc = (uint8_t)(crc << 1);
			}
		}
	}
	return crc;
}

static int read_byte(const struct bq769x0_config *config, uint8_t reg, uint8_t *out)
{
	int rc = i2c_reg_read_byte_dt(&config->i2c, reg, out);

	if (rc != 0) {
		LOG_ERR("I2C read reg 0x%02x failed: %d", reg, rc);
	}
	return rc;
}

static int write_byte(const struct bq769x0_config *config, uint8_t reg, uint8_t value)
{
	/* CRC 写帧覆盖 [I2C 写地址字节, reg, value];读方向不带 CRC。 */
	uint8_t crc_input[3] = { (uint8_t)(config->i2c.addr << 1), reg, value };
	uint8_t frame[3] = { reg, value, crc8(crc_input, sizeof(crc_input)) };
	int rc = i2c_write_dt(&config->i2c, frame, sizeof(frame));

	if (rc != 0) {
		LOG_ERR("I2C write reg 0x%02x failed: %d", reg, rc);
	}
	return rc;
}

static int read_word(const struct bq769x0_config *config, uint8_t reg, uint16_t *out)
{
	uint8_t hi;
	uint8_t lo;
	int rc;

	rc = read_byte(config, reg, &hi);
	if (rc != 0) {
		return rc;
	}
	rc = read_byte(config, reg + 1U, &lo);
	if (rc != 0) {
		return rc;
	}
	*out = (uint16_t)(((uint16_t)hi << 8) | lo);
	return 0;
}

/* ---- 单位转换 ---- */

static uint16_t raw14_to_mv(const struct bq769x0_data *data, uint16_t raw)
{
	/* 14 位 ADC 码 -> mV;截断除法是对外契约(测试台按同一算式生成期望值)。 */
	int32_t mv = ((int32_t)(raw & 0x3FFFU) * data->adc_gain_uv) / 1000 +
		     data->adc_offset_mv;

	return (uint16_t)CLAMP(mv, 0, UINT16_MAX);
}

static uint16_t ts_raw_to_mv(const struct bq769x0_data *data, uint16_t raw)
{
	/*
	 * TS 通道分压(mV):只做芯片 ADC 增益换算,不加 adc_offset ——offset 是电芯
	 * 电压零点校准,热敏分压不适用。怎么把这 mV 变温度是 bms_ntc 的事(SRP)。
	 */
	uint32_t mv = ((uint32_t)(raw & 0x3FFFU) * data->adc_gain_uv) / 1000U;

	return (uint16_t)MIN(mv, (uint32_t)UINT16_MAX);
}

/* 连读一段连续寄存器:BQ769x0 读方向地址自动递增,一笔事务拿回整块。 */
static int read_block(const struct bq769x0_config *config, uint8_t start_reg,
		      uint8_t *buf, size_t len)
{
	int rc = i2c_burst_read_dt(&config->i2c, start_reg, buf, len);

	if (rc != 0) {
		LOG_ERR("I2C burst read reg 0x%02x len %u failed: %d",
			start_reg, (unsigned)len, rc);
	}
	return rc;
}

/* ---- 内部序列(调用方必须已持锁) ---- */

static int read_cells_locked(const struct device *dev, uint16_t *cell_mv, size_t cell_count)
{
	const struct bq769x0_config *config = dev->config;
	const struct bq769x0_data *data = dev->data;
	/* 每通道 4 个流字节:VCn_HI, CRC, VCn_LO, CRC(见下方 CRC 说明)。 */
	uint8_t block[BQ769X0_MAX_CELLS * 4U];
	uint8_t max_channel = 0U;
	int rc;

	if ((cell_mv == NULL) || (cell_count > config->cell_count) ||
	    (config->cell_count > BQ769X0_MAX_CELLS) ||
	    (config->cell_channel_map_len != config->cell_count)) {
		LOG_ERR("read_cells EINVAL: req=%u cfg_count=%u map_len=%u",
			(unsigned)cell_count, config->cell_count,
			config->cell_channel_map_len);
		return -EINVAL;
	}

	/*
	 * 逐字节读(9 节 = 18 笔事务)在位翻转 I2C 上要 ~69ms。改成一笔 burst,
	 * 从 VC1_HI 连读到映射用到的最高物理通道,信封开销从 18 套降到 1 套;
	 * 中间没装电芯的槽位一并读进来、按映射取值时丢弃。
	 *
	 * CRC 坑:芯片写方向带 CRC(说明 CRC_EN=1),于是连续读时每个数据字节后
	 * 都会跟一个 CRC 字节。单字节读靠读完即 NACK 躲开了它(厂家就这么做的,
	 * reads_use_crc=false)。burst 躲不开,所以流格式是 [数据, CRC, 数据, CRC…],
	 * 每个通道占 4 字节;这里跨过 CRC 取数据字节(不校验,沿用厂家口径)。
	 */
	for (size_t i = 0U; i < cell_count; ++i) {
		uint8_t channel = config->cell_channel_map[i];

		if ((channel == 0U) || (channel > BQ769X0_MAX_CELLS)) {
			return -EINVAL;
		}
		if (channel > max_channel) {
			max_channel = channel;
		}
	}

	rc = read_block(config, REG_VC1_HI, block, (size_t)max_channel * 4U);
	if (rc != 0) {
		return rc;
	}

	for (size_t i = 0U; i < cell_count; ++i) {
		size_t off = (size_t)(config->cell_channel_map[i] - 1U) * 4U;
		uint16_t raw = (uint16_t)(((uint16_t)block[off] << 8) | block[off + 2U]);

		cell_mv[i] = raw14_to_mv(data, raw);
	}
	return 0;
}

static int update_sys_ctrl2_locked(const struct bq769x0_config *config, uint8_t mask, bool en)
{
	uint8_t value;
	int rc;

	rc = read_byte(config, REG_SYS_CTRL2, &value);
	if (rc != 0) {
		return rc;
	}
	/* 保留无关位,库仑计使能位始终保持置位(vendor 口径 0x40 基值) */
	value |= SYS_CTRL2_CC_EN;
	if (en) {
		value |= mask;
	} else {
		value &= (uint8_t)~mask;
	}
	return write_byte(config, REG_SYS_CTRL2, value);
}

/* ---- bms_afe_driver_api 实现 ---- */

static int bq769x0_init(const struct device *dev)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	uint8_t gain1;
	uint8_t gain2;
	uint8_t offset;
	uint8_t value;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);

	LOG_INF("AFE init: addr=0x%02x wake_gpio=%s", config->i2c.addr,
		(config->wake_gpio.port != NULL) ? "yes" : "no");

	/* 唤醒脉冲:拉高再放开(板级事实:PA8 pulse_high_then_low) */
	if (config->wake_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->wake_gpio)) {
			LOG_ERR("wake gpio not ready");
			rc = -ENODEV;
			goto out;
		}
		rc = gpio_pin_configure_dt(&config->wake_gpio, GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			goto out;
		}
		(void)gpio_pin_set_dt(&config->wake_gpio, 1);
		k_sleep(K_MSEC(100));
		(void)gpio_pin_set_dt(&config->wake_gpio, 0);
		k_sleep(K_MSEC(10));
	}

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("i2c bus not ready");
		rc = -ENODEV;
		goto out;
	}

	/* ADC 增益/偏移校准量:采样换算全程复用 */
	rc = read_byte(config, REG_ADCGAIN1, &gain1);
	if (rc != 0) {
		goto out;
	}
	rc = read_byte(config, REG_ADCGAIN2, &gain2);
	if (rc != 0) {
		goto out;
	}
	rc = read_byte(config, REG_ADCOFFSET, &offset);
	if (rc != 0) {
		goto out;
	}
	data->adc_gain_uv = (uint16_t)(ADC_GAIN_BASE_UV +
				       ((uint16_t)(gain1 & 0x0CU) << 1) +
				       ((uint16_t)(gain2 & 0xE0U) >> 5));
	data->adc_offset_mv = (int8_t)offset;

	/* 上电状态先留档再清除(SYS_STAT 写 1 清除) */
	rc = read_byte(config, REG_SYS_STAT, &data->boot_sys_stat);
	if (rc != 0) {
		goto out;
	}
	(void)write_byte(config, REG_SYS_STAT, 0xFFU);

	/* ADC 使能 + 外部热敏通道 */
	rc = read_byte(config, REG_SYS_CTRL1, &value);
	if (rc != 0) {
		goto out;
	}
	rc = write_byte(config, REG_SYS_CTRL1,
			value | SYS_CTRL1_ADC_EN | SYS_CTRL1_TEMP_SEL);
	if (rc != 0) {
		goto out;
	}
	/*
	 * 回读确认 ADC_EN 真写进去了。位翻转 I2C 的写**偶发不生效**,ADC_EN 没置上会让
	 * VC/TS ADC 不转换 → 电芯/温度全读 0(而库仑计 CC 是另一条通路,电流照常出)。
	 * 复位后间歇性"电芯读 0"就是这个。没置上就重写,最多 5 次。
	 */
	for (int adc_try = 0; adc_try < 5; ++adc_try) {
		uint8_t ctrl1 = 0U;

		if ((read_byte(config, REG_SYS_CTRL1, &ctrl1) == 0) &&
		    ((ctrl1 & SYS_CTRL1_ADC_EN) != 0U)) {
			break;
		}
		(void)write_byte(config, REG_SYS_CTRL1,
				 value | SYS_CTRL1_ADC_EN | SYS_CTRL1_TEMP_SEL);
		k_sleep(K_MSEC(5));
	}

	/* 库仑计使能 */
	rc = read_byte(config, REG_SYS_CTRL2, &value);
	if (rc != 0) {
		goto out;
	}
	rc = write_byte(config, REG_SYS_CTRL2, value | SYS_CTRL2_CC_EN);
	if (rc != 0) {
		goto out;
	}

	rc = write_byte(config, REG_CC_CFG, CC_CFG_DEFAULT);

	/*
	 * ADC 使能后给一轮转换的时间再让采样任务读,否则首帧 VC/TS 可能读到 0。
	 * (排查"电芯读 0"时发现:enable 后立刻读会拿到未刷新的 0;等 ~250ms 稳。)
	 */
	if (rc == 0) {
		k_sleep(K_MSEC(250));
	}

out:
	if (rc == 0) {
		LOG_INF("AFE init OK: gain_uv=%u offset_mv=%d boot_stat=0x%02x",
			data->adc_gain_uv, data->adc_offset_mv, data->boot_sys_stat);
	} else {
		LOG_ERR("AFE init FAILED: rc=%d", rc);
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

static int bq769x0_read_cells(const struct device *dev, uint16_t *cell_mv, size_t cell_count)
{
	struct bq769x0_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = read_cells_locked(dev, cell_mv, cell_count);
	k_mutex_unlock(&data->lock);
	return rc;
}

static int bq769x0_read_pack_voltage(const struct device *dev, uint16_t *pack_mv)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	uint16_t cells[BQ769X0_MAX_CELLS];
	uint32_t total = 0U;
	int rc;

	if (pack_mv == NULL) {
		return -EINVAL;
	}
	/* 包压 = 逻辑电芯之和(vendor 同款口径),不用 BAT 寄存器(含未用物理口)。 */
	k_mutex_lock(&data->lock, K_FOREVER);
	rc = read_cells_locked(dev, cells, config->cell_count);
	k_mutex_unlock(&data->lock);
	if (rc != 0) {
		return rc;
	}
	for (size_t i = 0U; i < config->cell_count; ++i) {
		total += cells[i];
	}
	*pack_mv = (uint16_t)MIN(total, UINT16_MAX);
	return 0;
}

static int bq769x0_read_current(const struct device *dev, int16_t *current_ma)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	uint16_t raw;
	int32_t converted;
	int rc;

	if (current_ma == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	rc = read_word(config, REG_CC_HI, &raw);
	k_mutex_unlock(&data->lock);
	if (rc != 0) {
		return rc;
	}
	/*
	 * CC 是 16 位二补码。板级事实:这块板 SRP/SRN 接法让 BQ 的 CC **充电为正**,
	 * 而本固件全局约定 **放电为正、充电为负**(充放电状态机/OCD-OCC/SOC 库仑积分都按
	 * 这个),故取反对齐。真机实证:充电(电芯电压上涨)时未取反读到 +542mA,取反后为负。
	 * 详见 BoardFacts 电流符号一节。截断除法与测试台生成侧一致。
	 */
	converted = -(((int32_t)(int16_t)raw * (int32_t)config->current_lsb_ua) / 1000);
	*current_ma = (int16_t)CLAMP(converted, INT16_MIN, INT16_MAX);
	return 0;
}

static int bq769x0_read_register(const struct device *dev, uint8_t reg, uint8_t *value)
{
	struct bq769x0_data *data = dev->data;
	int rc;

	if (value == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	rc = read_byte(dev->config, reg, value);
	k_mutex_unlock(&data->lock);
	return rc;
}

static int bq769x0_read_thermistor_mv(const struct device *dev, uint16_t *ts_mv,
				      size_t count)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	static const uint8_t temp_regs[BQ769X0_MAX_TS_CHANNELS] = {
		REG_TS1_HI, REG_TS2_HI, REG_TS3_HI,
	};
	int rc = 0;

	if ((ts_mv == NULL) || (count > config->temp_count) ||
	    (count > BQ769X0_MAX_TS_CHANNELS)) {
		return -EINVAL;
	}

	/* 只回 TS 分压 mV;热敏曲线/开短路诊断在 bms_ntc 里做(SRP 解耦)。 */
	k_mutex_lock(&data->lock, K_FOREVER);
	for (size_t i = 0U; i < count; ++i) {
		uint16_t raw;

		rc = read_word(config, temp_regs[i], &raw);
		if (rc != 0) {
			break;
		}
		ts_mv[i] = ts_raw_to_mv(data, raw);
	}
	k_mutex_unlock(&data->lock);

	return rc;
}

static int bq769x0_read_fault_status(const struct device *dev, uint16_t *fault_bits)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	uint8_t value;
	int rc;

	if (fault_bits == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	rc = read_byte(config, REG_SYS_STAT, &value);
	k_mutex_unlock(&data->lock);
	if (rc != 0) {
		return rc;
	}
	/*
	 * CC_READY(bit7)是库仑计"新读数就绪"标志、每 ~250ms 置一次,不是故障。只从
	 * 返回值里**剔除**它,不上报为 AFE 报警(否则上层 bms_protection_policy 会把
	 * "SYS_STAT 非零"当报警一直误锁、把充放 FET 全关)。
	 *
	 * 不再写 1 去清它:只读不写更安全——位翻转 I2C 上偶发写歪寄存器地址会误改
	 * SYS_CTRL1、清掉 ADC_EN 让电芯读 0(排查过)。CC_READY 挂着无妨,反正已剔除。
	 */
	*fault_bits = (uint16_t)(value & (uint8_t)~SYS_STAT_CC_READY);
	return 0;
}

static int bq769x0_clear_fault_status(const struct device *dev, uint16_t fault_bits)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = write_byte(config, REG_SYS_STAT, (uint8_t)(fault_bits & 0xFFU));
	k_mutex_unlock(&data->lock);
	return rc;
}

static int bq769x0_set_balance_mask(const struct device *dev, uint16_t cell_mask)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	uint16_t allowed;
	uint16_t physical_mask = 0U;
	int rc;

	if ((config->cell_count == 0U) || (config->cell_count > BQ769X0_MAX_CELLS) ||
	    (config->cell_channel_map_len != config->cell_count)) {
		return -EINVAL;
	}
	/*
	 * 应用层传自然电芯位图(bit0 = 第 1 节);先映射回物理 VC 槽位,
	 * 再拆成三个 5 位寄存器。相邻互斥等业务规则在均衡策略层已处理。
	 */
	allowed = (uint16_t)(BIT(config->cell_count) - 1U);
	cell_mask &= allowed;
	for (size_t i = 0U; i < config->cell_count; ++i) {
		uint8_t channel = config->cell_channel_map[i];

		if ((channel == 0U) || (channel > BQ769X0_MAX_CELLS)) {
			return -EINVAL;
		}
		if ((cell_mask & (uint16_t)BIT(i)) != 0U) {
			physical_mask |= (uint16_t)BIT(channel - 1U);
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = write_byte(config, REG_CELLBAL1, (uint8_t)(physical_mask & 0x1FU));
	if (rc == 0) {
		rc = write_byte(config, REG_CELLBAL2, (uint8_t)((physical_mask >> 5) & 0x1FU));
	}
	if (rc == 0) {
		rc = write_byte(config, REG_CELLBAL3, (uint8_t)((physical_mask >> 10) & 0x1FU));
	}
	k_mutex_unlock(&data->lock);
	return rc;
}

static int bq769x0_set_chg_enabled(const struct device *dev, bool enabled)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = update_sys_ctrl2_locked(config, SYS_CTRL2_CHG_ON, enabled);
	k_mutex_unlock(&data->lock);
	return rc;
}

static int bq769x0_set_dsg_enabled(const struct device *dev, bool enabled)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);
	rc = update_sys_ctrl2_locked(config, SYS_CTRL2_DSG_ON, enabled);
	k_mutex_unlock(&data->lock);
	return rc;
}

static int bq769x0_read_switch_state(const struct device *dev, BmsSwitchState *out)
{
	const struct bq769x0_config *config = dev->config;
	struct bq769x0_data *data = dev->data;
	uint8_t value;
	int rc;

	if (out == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	rc = read_byte(config, REG_SYS_CTRL2, &value);
	k_mutex_unlock(&data->lock);
	if (rc != 0) {
		return rc;
	}
	out->chg_enabled = (value & SYS_CTRL2_CHG_ON) != 0U;
	out->dsg_enabled = (value & SYS_CTRL2_DSG_ON) != 0U;
	return 0;
}

int bq769x0_device_init(const struct device *dev)
{
	struct bq769x0_data *data = dev->data;

	/*
	 * device init level 只做对象自身构造(锁初始化)。芯片唤醒与寄存器配置
	 * 放在 api->init,由装配根在应用初始化阶段显式调用(唤醒脉冲需 k_sleep)。
	 */
	return k_mutex_init(&data->lock);
}

/* 家族共享虚表:BQ76940 / BQ76930 都注册它 */
const struct bms_afe_driver_api bq769x0_afe_api = {
	.init = bq769x0_init,
	.measure = {
		.read_cells = bq769x0_read_cells,
		.read_pack_voltage = bq769x0_read_pack_voltage,
		.read_current = bq769x0_read_current,
		.read_register = bq769x0_read_register,
		.read_thermistor_mv = bq769x0_read_thermistor_mv,
	},
	.protect = {
		.read_fault_status = bq769x0_read_fault_status,
		.clear_fault_status = bq769x0_clear_fault_status,
	},
	.balance = {
		.set_balance_mask = bq769x0_set_balance_mask,
	},
	.power = {
		.set_chg_enabled = bq769x0_set_chg_enabled,
		.set_dsg_enabled = bq769x0_set_dsg_enabled,
		.read_switch_state = bq769x0_read_switch_state,
	},
};
