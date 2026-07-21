#include "afe/bms_afe.h"

#include <errno.h>

/*
 * facade -> driver_api 派发层。
 * 统一做空指针与缺失 op 检查;缺失的能力返回 -ENOSYS,
 * 让上层可以区分"设备不支持"和"传输失败"。
 */
static const struct bms_afe_driver_api *bms_afe_api(const struct device *dev)
{
	if ((dev == NULL) || (dev->api == NULL)) {
		return NULL;
	}
	return (const struct bms_afe_driver_api *)dev->api;
}

#define BMS_AFE_DISPATCH(dev, op, ...)                                             \
	do {                                                                       \
		const struct bms_afe_driver_api *api = bms_afe_api(dev);           \
		if (api == NULL) {                                                 \
			return -EINVAL;                                            \
		}                                                                  \
		if (api->op == NULL) {                                             \
			return -ENOSYS;                                            \
		}                                                                  \
		return api->op(dev, ##__VA_ARGS__);                                \
	} while (0)

int bms_afe_init(const struct device *dev)
{
	BMS_AFE_DISPATCH(dev, init);
}

int bms_afe_read_cells(const struct device *dev, uint16_t *cell_mv, size_t cell_count)
{
	BMS_AFE_DISPATCH(dev, measure.read_cells, cell_mv, cell_count);
}

int bms_afe_read_pack_voltage(const struct device *dev, uint16_t *pack_mv)
{
	BMS_AFE_DISPATCH(dev, measure.read_pack_voltage, pack_mv);
}

int bms_afe_read_current(const struct device *dev, int16_t *current_ma)
{
	BMS_AFE_DISPATCH(dev, measure.read_current, current_ma);
}

int bms_afe_read_register(const struct device *dev, uint8_t reg, uint8_t *value)
{
	BMS_AFE_DISPATCH(dev, measure.read_register, reg, value);
}

int bms_afe_read_thermistor_mv(const struct device *dev,
			       uint16_t *ts_mv,
			       size_t count)
{
	BMS_AFE_DISPATCH(dev, measure.read_thermistor_mv, ts_mv, count);
}

int bms_afe_read_fault_status(const struct device *dev, uint16_t *fault_bits)
{
	BMS_AFE_DISPATCH(dev, protect.read_fault_status, fault_bits);
}

int bms_afe_clear_fault_status(const struct device *dev, uint16_t fault_bits)
{
	BMS_AFE_DISPATCH(dev, protect.clear_fault_status, fault_bits);
}

int bms_afe_set_balance_mask(const struct device *dev, uint16_t cell_mask)
{
	BMS_AFE_DISPATCH(dev, balance.set_balance_mask, cell_mask);
}

int bms_afe_set_chg_enabled(const struct device *dev, bool enabled)
{
	BMS_AFE_DISPATCH(dev, power.set_chg_enabled, enabled);
}

int bms_afe_set_dsg_enabled(const struct device *dev, bool enabled)
{
	BMS_AFE_DISPATCH(dev, power.set_dsg_enabled, enabled);
}

int bms_afe_read_switch_state(const struct device *dev, BmsSwitchState *out)
{
	BMS_AFE_DISPATCH(dev, power.read_switch_state, out);
}
