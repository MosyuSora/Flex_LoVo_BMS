#ifndef BMS_AFE_H
#define BMS_AFE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/*
 * ============================================================================
 * BMS AFE 设备接口(driver_api 抽象 + 薄分发)
 * ============================================================================
 *
 * 这是一颗"模拟前端(AFE)"能干什么的**纯接口**:一张 driver_api 虚表 + 一组薄
 * 分发函数。它只定义能力,不含任何实现,也不认任何传输方式(I2C / 菊链都行)。
 * 接口与具体设备实现同住 drivers/afe/,消费者用 "afe/bms_afe.h" 引入。
 *
 * 多态关系(参照 Zephyr 的 i2c 子系统):
 *   bms_afe_driver_api(本文件)  ←── 抽象接口:一套虚表,多设备复用
 *     ├── bq769x0 家族驱动        ←── 设备驱动:BQ76940 / BQ76930(I2C)
 *     └── bq79616 驱动            ←── 设备驱动:车规菊链(骨架)
 *   具体芯片"继承"本接口 = 填这张虚表并用 DEVICE_DT_INST_DEFINE 注册;
 *   传输(I2C 等)是**设备驱动内部的成员(组合)**,不是本接口的一部分——
 *   AFE 芯片"用"一条 I2C,而不是"是"一条 I2C。
 *
 * 传感器解耦:温度只回 TS 分压 mV,热敏电阻曲线/开短路诊断在
 * drivers/sensor/bms_ntc 里做(换 NTC 型号不碰芯片驱动)。
 *
 * FET(CHG/DSG)控制归属:BQ769x0 家族的充放电 MOS 就是芯片自身经 SYS_CTRL2
 * 驱动的,属于 AFE 的固有能力,所以放在本接口里(不单独抽 MOS 接口)。若某颗
 * 芯片不具备某项能力,驱动把对应 op 留空即可,分发层返回 -ENOSYS。
 *
 * 并发约定:驱动内部必须自带互斥,保证每个 op 的多寄存器序列原子;上层可以
 * 从多个线程直接调用这些接口。
 */

/* CHG/DSG FET 的读回状态。FET 是 AFE 的固有能力,故类型定义在本接口内。 */
typedef struct BmsSwitchState {
	bool chg_enabled;
	bool dsg_enabled;
} BmsSwitchState;

/* ---- driver_api:一颗 AFE 应实现的虚表(全必填 = 接口合同) ---- */
typedef int (*bms_afe_api_init_t)(const struct device *dev);
typedef int (*bms_afe_api_read_cells_t)(const struct device *dev,
					uint16_t *cell_mv,
					size_t cell_count);
typedef int (*bms_afe_api_read_pack_voltage_t)(const struct device *dev,
					       uint16_t *pack_mv);
typedef int (*bms_afe_api_read_current_t)(const struct device *dev,
					  int16_t *current_ma);
typedef int (*bms_afe_api_read_register_t)(const struct device *dev,
					   uint8_t reg, uint8_t *value);
/*
 * 只回 TS 通道的分压(mV,已按芯片 ADC 增益换算)。热敏电阻曲线、开短路诊断
 * 不在 AFE 里做——那是传感器知识,归 drivers/sensor/bms_ntc(SRP 解耦)。
 * ts_mv[i] 对应第 i 个热敏通道;count 不得超过 temp-count。
 */
typedef int (*bms_afe_api_read_thermistor_mv_t)(const struct device *dev,
						uint16_t *ts_mv,
						size_t count);
typedef int (*bms_afe_api_read_fault_status_t)(const struct device *dev,
					       uint16_t *fault_bits);
typedef int (*bms_afe_api_clear_fault_status_t)(const struct device *dev,
						uint16_t fault_bits);
typedef int (*bms_afe_api_set_balance_mask_t)(const struct device *dev,
					      uint16_t cell_mask);
typedef int (*bms_afe_api_set_switch_t)(const struct device *dev, bool enabled);
typedef int (*bms_afe_api_read_switch_state_t)(const struct device *dev,
					       BmsSwitchState *out);

/*
 * ISP:把 12 个能力按"变更理由"分成四组子接口。一颗 AFE 芯片同时实现四组
 * (它物理上就是一颗多功能器件),但分组让"谁用哪一类能力"一目了然,也方便
 * 未来某类消费者只依赖其中一组做推理。分发函数才是真正的调用边界(下方),
 * 上层不直接碰这张表。
 */
struct bms_afe_measure_api { /* 量测:电压/电流/温度/寄存器 */
	bms_afe_api_read_cells_t read_cells;
	bms_afe_api_read_pack_voltage_t read_pack_voltage;
	bms_afe_api_read_current_t read_current;
	bms_afe_api_read_register_t read_register;
	bms_afe_api_read_thermistor_mv_t read_thermistor_mv;
};

struct bms_afe_protect_api { /* 保护:芯片自主报警状态的读/清 */
	bms_afe_api_read_fault_status_t read_fault_status;
	bms_afe_api_clear_fault_status_t clear_fault_status;
};

struct bms_afe_balance_api { /* 均衡:被动均衡通道使能 */
	bms_afe_api_set_balance_mask_t set_balance_mask;
};

struct bms_afe_power_api { /* 执行器:CHG/DSG FET 使能与回读(AFE 固有能力) */
	bms_afe_api_set_switch_t set_chg_enabled;
	bms_afe_api_set_switch_t set_dsg_enabled;
	bms_afe_api_read_switch_state_t read_switch_state;
};

struct bms_afe_driver_api {
	bms_afe_api_init_t init;
	struct bms_afe_measure_api measure;
	struct bms_afe_protect_api protect;
	struct bms_afe_balance_api balance;
	struct bms_afe_power_api power;
};

/* ---- 薄分发函数:上层只认 const struct device *,不认具体芯片 ---- */
int bms_afe_init(const struct device *dev);
int bms_afe_read_cells(const struct device *dev, uint16_t *cell_mv, size_t cell_count);
int bms_afe_read_pack_voltage(const struct device *dev, uint16_t *pack_mv);
int bms_afe_read_current(const struct device *dev, int16_t *current_ma);
int bms_afe_read_register(const struct device *dev, uint8_t reg, uint8_t *value);
int bms_afe_read_thermistor_mv(const struct device *dev,
			       uint16_t *ts_mv,
			       size_t count);
int bms_afe_read_fault_status(const struct device *dev, uint16_t *fault_bits);
int bms_afe_clear_fault_status(const struct device *dev, uint16_t fault_bits);
int bms_afe_set_balance_mask(const struct device *dev, uint16_t cell_mask);
int bms_afe_set_chg_enabled(const struct device *dev, bool enabled);
int bms_afe_set_dsg_enabled(const struct device *dev, bool enabled);
int bms_afe_read_switch_state(const struct device *dev, BmsSwitchState *out);

#endif
