#ifndef BMS_CHARGE_POLICY_H
#define BMS_CHARGE_POLICY_H

#include "core/bms_power_state.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * 充放电状态分类策略:静置带 ± 滞回。
 *
 * 进入充/放电要求 |电流| 超过 (静置阈值 + 滞回);
 * 退出回静置要求 |电流| 低于 (静置阈值 - 滞回)。
 * 两个门槛之间是保持区,消除阈值附近的模式抖动。
 */
typedef struct BmsChargePolicy {
	BmsPowerMode mode;
} BmsChargePolicy;

void bms_charge_policy_init(BmsChargePolicy *policy);

BmsPowerMode bms_charge_policy_classify(BmsChargePolicy *policy,
					int16_t current_ma,
					bool fault_latched);

#endif
