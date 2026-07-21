#include "algo/bms_charge_policy.h"

#include "config/appconfig.h"

#include <stddef.h>

#define ENTER_MA (BMS_APP_CURRENT_IDLE_THRESHOLD_MA + BMS_APP_CURRENT_IDLE_HYST_MA)
#define EXIT_MA (BMS_APP_CURRENT_IDLE_THRESHOLD_MA - BMS_APP_CURRENT_IDLE_HYST_MA)

void bms_charge_policy_init(BmsChargePolicy *policy)
{
	if (policy == NULL) {
		return;
	}
	policy->mode = BMS_POWER_MODE_IDLE;
}

BmsPowerMode bms_charge_policy_classify(BmsChargePolicy *policy,
					int16_t current_ma,
					bool fault_latched)
{
	if (policy == NULL) {
		return BMS_POWER_MODE_IDLE;
	}

	/* 故障态优先;解除后按进入门槛重新分类,不继承故障前的模式 */
	if (fault_latched) {
		policy->mode = BMS_POWER_MODE_FAULT;
		return policy->mode;
	}

	switch (policy->mode) {
	case BMS_POWER_MODE_CHARGING:
		/* 电流约定:充电为负 */
		if (current_ma > -EXIT_MA) {
			policy->mode = BMS_POWER_MODE_IDLE;
		}
		break;
	case BMS_POWER_MODE_DISCHARGING:
		if (current_ma < EXIT_MA) {
			policy->mode = BMS_POWER_MODE_IDLE;
		}
		break;
	default:
		policy->mode = BMS_POWER_MODE_IDLE;
		break;
	}

	if (policy->mode == BMS_POWER_MODE_IDLE) {
		if (current_ma <= -ENTER_MA) {
			policy->mode = BMS_POWER_MODE_CHARGING;
		} else if (current_ma >= ENTER_MA) {
			policy->mode = BMS_POWER_MODE_DISCHARGING;
		}
	}

	return policy->mode;
}
