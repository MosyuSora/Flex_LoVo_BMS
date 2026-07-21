#include "algo/bms_balance_policy.h"

#include "config/appconfig.h"

#include <stddef.h>
#include <stdlib.h>
#include <zephyr/sys/util.h>

static uint16_t balance_filter_adjacent(uint16_t mask)
{
	uint16_t filtered = 0U;

	/*
	 * BQ769x0 被动均衡不允许相邻通道同时开(散热约束)。
	 * 保留低序号候选,抑制紧邻的高序号候选。
	 */
	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		uint16_t bit = (uint16_t)BIT(i);

		if ((mask & bit) == 0U) {
			continue;
		}
		if ((i > 0U) && ((filtered & (uint16_t)BIT(i - 1U)) != 0U)) {
			continue;
		}
		filtered |= bit;
	}

	return filtered;
}

uint16_t bms_balance_policy_select_mask(const BmsSampleSnapshot *sample,
					const BmsFaultState *fault,
					uint16_t previous_mask,
					BmsBalanceState *state_out)
{
	uint16_t min_mv = UINT16_MAX;
	uint16_t max_mv = 0U;
	uint16_t mask = 0U;

	if ((sample == NULL) || (fault == NULL) || (state_out == NULL)) {
		return 0U;
	}

	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		min_mv = MIN(min_mv, sample->cell_mv[i]);
		max_mv = MAX(max_mv, sample->cell_mv[i]);
	}
	state_out->min_cell_mv = min_mv;
	state_out->max_cell_mv = max_mv;
	state_out->delta_mv = (uint16_t)(max_mv - min_mv);

	/*
	 * 均衡是慢动作,安全门优先:电流大(采样精度差且发热)、温度出保护窗、以及
	 * 除过压外的故障都不许开。
	 *
	 * **过压是例外**:OV = "某节太高",放掉它正是解 OV 的手段。若因 OV latched 就停
	 * 均衡,会和"OV 要靠均衡把高单体降到恢复门槛以下才能解除"死锁——高单体一直卡在
	 * 恢复门槛、OV 永不恢复、均衡永不开。故这里只屏蔽"除 OV 外"的故障。
	 */
	if (((fault->fault_bits & ~(uint32_t)BMS_FAULT_CELL_OV) != 0U) ||
	    (abs(sample->current_ma) > BMS_APP_BALANCE_MAX_ABS_CURRENT_MA)) {
		return 0U;
	}
	for (size_t i = 0U; i < BMS_APP_PROT_TEMP_CHANNELS; ++i) {
		if ((sample->temperature_cdeg[i] < BMS_APP_DSG_UT_CDEG) ||
		    (sample->temperature_cdeg[i] > BMS_APP_CHG_OT_CDEG)) {
			return 0U;
		}
	}

	/* 压差滞回:低于 OFF 全关,OFF..ON 之间维持现状,超过 ON 重新选 */
	if (state_out->delta_mv <= BMS_APP_BALANCE_DELTA_OFF_MV) {
		return 0U;
	}
	if (state_out->delta_mv < BMS_APP_BALANCE_DELTA_ON_MV) {
		return previous_mask;
	}

	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		if ((sample->cell_mv[i] >= BMS_APP_BALANCE_MIN_CELL_MV) &&
		    ((uint16_t)(sample->cell_mv[i] - min_mv) >=
		     BMS_APP_BALANCE_DELTA_ON_MV)) {
			mask |= (uint16_t)BIT(i);
		}
	}

	return balance_filter_adjacent(mask);
}
