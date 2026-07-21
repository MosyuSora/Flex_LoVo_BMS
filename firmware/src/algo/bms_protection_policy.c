#include "algo/bms_protection_policy.h"

#include "config/appconfig.h"

#include <stddef.h>
#include <string.h>

/*
 * 每类故障的静态描述:触发时长 + 对应的故障位。
 * 触发/恢复条件本身依赖快照的不同字段,用函数表达。
 */
struct prot_class_desc {
	uint32_t fault_bit;
	uint32_t trip_ms;
};

static const struct prot_class_desc PROT_DESC[BMS_PROT_CLASS_COUNT] = {
	[BMS_PROT_OV] = { BMS_FAULT_CELL_OV, BMS_APP_PROT_TRIP_MS },
	[BMS_PROT_UV] = { BMS_FAULT_CELL_UV, BMS_APP_PROT_TRIP_MS },
	[BMS_PROT_DSG_OC] = { BMS_FAULT_DISCHARGE_OC, BMS_APP_PROT_OC_TRIP_MS },
	[BMS_PROT_CHG_OC] = { BMS_FAULT_CHARGE_OC, BMS_APP_PROT_OC_TRIP_MS },
	[BMS_PROT_CHG_OT] = { BMS_FAULT_CHG_OT, BMS_APP_PROT_TRIP_MS },
	[BMS_PROT_CHG_UT] = { BMS_FAULT_CHG_UT, BMS_APP_PROT_TRIP_MS },
	[BMS_PROT_DSG_OT] = { BMS_FAULT_DSG_OT, BMS_APP_PROT_TRIP_MS },
	[BMS_PROT_DSG_UT] = { BMS_FAULT_DSG_UT, BMS_APP_PROT_TRIP_MS },
};

static uint16_t sample_max_cell(const BmsSampleSnapshot *sample)
{
	uint16_t max_mv = 0U;

	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		if (sample->cell_mv[i] > max_mv) {
			max_mv = sample->cell_mv[i];
		}
	}
	return max_mv;
}

static uint16_t sample_min_cell(const BmsSampleSnapshot *sample)
{
	uint16_t min_mv = UINT16_MAX;

	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		if (sample->cell_mv[i] < min_mv) {
			min_mv = sample->cell_mv[i];
		}
	}
	return min_mv;
}

/* 参与保护的通道是否可信(既装了 NTC、又未开短路) */
static bool temp_channel_trusted(const BmsSampleSnapshot *sample, size_t i)
{
	return (sample->temp_valid & (uint8_t)(1U << i)) != 0U;
}

/* 是否存在参与保护但读数不可信的 NTC(开路/短路) */
static bool any_prot_temp_untrusted(const BmsSampleSnapshot *sample)
{
	for (size_t i = 0U; i < BMS_APP_PROT_TEMP_CHANNELS; ++i) {
		if (!temp_channel_trusted(sample, i)) {
			return true;
		}
	}
	return false;
}

/*
 * 温度越限检查只采信可信通道(见 appconfig 注释 + NTC 开短路诊断)。
 * 不可信通道不参与 OT/UT 判定,改由传感器故障分支处理。
 */
static bool any_temp_at_or_above(const BmsSampleSnapshot *sample, int16_t limit)
{
	for (size_t i = 0U; i < BMS_APP_PROT_TEMP_CHANNELS; ++i) {
		if (temp_channel_trusted(sample, i) &&
		    (sample->temperature_cdeg[i] >= limit)) {
			return true;
		}
	}
	return false;
}

static bool any_temp_at_or_below(const BmsSampleSnapshot *sample, int16_t limit)
{
	for (size_t i = 0U; i < BMS_APP_PROT_TEMP_CHANNELS; ++i) {
		if (temp_channel_trusted(sample, i) &&
		    (sample->temperature_cdeg[i] <= limit)) {
			return true;
		}
	}
	return false;
}

static bool all_temp_below(const BmsSampleSnapshot *sample, int16_t limit)
{
	return !any_temp_at_or_above(sample, limit);
}

static bool all_temp_above(const BmsSampleSnapshot *sample, int16_t limit)
{
	return !any_temp_at_or_below(sample, limit);
}

/* 触发条件:仍处于越限区 */
static bool prot_condition(BmsProtClass cls, const BmsSampleSnapshot *sample)
{
	switch (cls) {
	case BMS_PROT_OV:
		return sample_max_cell(sample) >= BMS_APP_CELL_OV_MV;
	case BMS_PROT_UV:
		return sample_min_cell(sample) <= BMS_APP_CELL_UV_MV;
	case BMS_PROT_DSG_OC:
		return sample->current_ma >= BMS_APP_CURRENT_OCD_MA;
	case BMS_PROT_CHG_OC:
		return sample->current_ma <= BMS_APP_CURRENT_OCC_MA;
	case BMS_PROT_CHG_OT:
		return any_temp_at_or_above(sample, BMS_APP_CHG_OT_CDEG);
	case BMS_PROT_CHG_UT:
		return any_temp_at_or_below(sample, BMS_APP_CHG_UT_CDEG);
	case BMS_PROT_DSG_OT:
		return any_temp_at_or_above(sample, BMS_APP_DSG_OT_CDEG);
	case BMS_PROT_DSG_UT:
		return any_temp_at_or_below(sample, BMS_APP_DSG_UT_CDEG);
	default:
		return false;
	}
}

/* 恢复条件:已回到回差以内(与触发条件之间留死区,防抖动) */
static bool prot_recovered(BmsProtClass cls, const BmsSampleSnapshot *sample)
{
	switch (cls) {
	case BMS_PROT_OV:
		return sample_max_cell(sample) <= BMS_APP_CELL_OV_RECOVER_MV;
	case BMS_PROT_UV:
		/* vendor UV 回差为 0:严格大于阈值即视为回归 */
		return sample_min_cell(sample) > BMS_APP_CELL_UV_RECOVER_MV;
	case BMS_PROT_DSG_OC:
		return sample->current_ma <= BMS_APP_CURRENT_OC_RECOVER_MA;
	case BMS_PROT_CHG_OC:
		return sample->current_ma >= -BMS_APP_CURRENT_OC_RECOVER_MA;
	case BMS_PROT_CHG_OT:
		return all_temp_below(sample,
				      BMS_APP_CHG_OT_CDEG - BMS_APP_TEMP_RECOVER_HYST_CDEG);
	case BMS_PROT_CHG_UT:
		return all_temp_above(sample,
				      BMS_APP_CHG_UT_CDEG + BMS_APP_TEMP_RECOVER_HYST_CDEG);
	case BMS_PROT_DSG_OT:
		return all_temp_below(sample,
				      BMS_APP_DSG_OT_CDEG - BMS_APP_TEMP_RECOVER_HYST_CDEG);
	case BMS_PROT_DSG_UT:
		return all_temp_above(sample,
				      BMS_APP_DSG_UT_CDEG + BMS_APP_TEMP_RECOVER_HYST_CDEG);
	default:
		return true;
	}
}

void bms_protection_policy_init(BmsProtectionPolicy *policy)
{
	if (policy == NULL) {
		return;
	}
	memset(policy, 0, sizeof(*policy));
}

void bms_protection_policy_request_clear(BmsProtectionPolicy *policy)
{
	if (policy == NULL) {
		return;
	}
	policy->clear_requested = true;
}

static void prot_update_class(BmsProtectionPolicy *policy,
			      BmsProtClass cls,
			      const BmsSampleSnapshot *sample,
			      uint32_t now_ms,
			      bool clear)
{
	const struct prot_class_desc *desc = &PROT_DESC[cls];
	bool cond = prot_condition(cls, sample);

	if ((policy->latched_bits & desc->fault_bit) != 0U) {
		policy->trip_pending[cls] = false;

		/* 外部清障:条件已消失的故障跳过恢复计时,立即解除 */
		if (clear && !cond) {
			policy->latched_bits &= ~desc->fault_bit;
			policy->recover_pending[cls] = false;
			return;
		}

		if (prot_recovered(cls, sample)) {
			if (!policy->recover_pending[cls]) {
				policy->recover_pending[cls] = true;
				policy->recover_since_ms[cls] = now_ms;
			} else if ((now_ms - policy->recover_since_ms[cls]) >=
				   BMS_APP_PROT_RECOVER_MS) {
				policy->latched_bits &= ~desc->fault_bit;
				policy->recover_pending[cls] = false;
			}
		} else {
			policy->recover_pending[cls] = false;
		}
		return;
	}

	policy->recover_pending[cls] = false;
	if (cond) {
		if (!policy->trip_pending[cls]) {
			policy->trip_pending[cls] = true;
			policy->trip_since_ms[cls] = now_ms;
		} else if ((now_ms - policy->trip_since_ms[cls]) >= desc->trip_ms) {
			policy->latched_bits |= desc->fault_bit;
			policy->trip_pending[cls] = false;
		}
	} else {
		policy->trip_pending[cls] = false;
	}
}

static void prot_update_afe(BmsProtectionPolicy *policy,
			    bool afe_read_ok,
			    uint16_t afe_status,
			    uint32_t now_ms,
			    bool clear)
{
	/* AFE 通信失败:连续 N 次判失联 */
	if (!afe_read_ok) {
		if (policy->comm_fail_count < 0xFFU) {
			policy->comm_fail_count++;
		}
		if (policy->comm_fail_count >= BMS_APP_PROT_AFE_COMM_FAIL_LIMIT) {
			policy->latched_bits |= BMS_FAULT_AFE_COMM;
		}
		policy->comm_recover_pending = false;
		return;
	}

	policy->comm_fail_count = 0U;
	if ((policy->latched_bits & BMS_FAULT_AFE_COMM) != 0U) {
		if (clear) {
			policy->latched_bits &= ~BMS_FAULT_AFE_COMM;
			policy->comm_recover_pending = false;
		} else if (!policy->comm_recover_pending) {
			policy->comm_recover_pending = true;
			policy->comm_recover_since_ms = now_ms;
		} else if ((now_ms - policy->comm_recover_since_ms) >=
			   BMS_APP_PROT_RECOVER_MS) {
			policy->latched_bits &= ~BMS_FAULT_AFE_COMM;
			policy->comm_recover_pending = false;
		}
	}

	/* AFE 硬件粘滞位:芯片自己已做判定,读到即刻上锁 */
	policy->last_afe_status = afe_status;
	if (afe_status != 0U) {
		policy->latched_bits |= BMS_FAULT_AFE_ALERT;
		policy->afe_recover_pending = false;
		return;
	}

	if ((policy->latched_bits & BMS_FAULT_AFE_ALERT) != 0U) {
		if (clear) {
			policy->latched_bits &= ~BMS_FAULT_AFE_ALERT;
			policy->afe_recover_pending = false;
		} else if (!policy->afe_recover_pending) {
			policy->afe_recover_pending = true;
			policy->afe_recover_since_ms = now_ms;
		} else if ((now_ms - policy->afe_recover_since_ms) >=
			   BMS_APP_PROT_RECOVER_MS) {
			policy->latched_bits &= ~BMS_FAULT_AFE_ALERT;
			policy->afe_recover_pending = false;
		}
	}
}

void bms_protection_policy_update(BmsProtectionPolicy *policy,
				  const BmsSampleSnapshot *sample,
				  bool afe_read_ok,
				  uint16_t afe_status,
				  uint32_t now_ms,
				  BmsFaultState *out)
{
	uint32_t fault_bits;
	bool clear;

	if ((policy == NULL) || (out == NULL)) {
		return;
	}

	memset(out, 0, sizeof(*out));

	/*
	 * 版本 0 = 还没有真实采样。全 0 初值不能当电池数据,
	 * 否则上电瞬间会误判欠压;此时禁止充放电并直接返回。
	 */
	if ((sample == NULL) || (sample->version == 0U)) {
		out->timestamp_ms = now_ms;
		out->fault_bits = BMS_FAULT_SAMPLE_INVALID;
		out->latched = true;
		out->charge_allowed = false;
		out->discharge_allowed = false;
		return;
	}

	clear = policy->clear_requested;
	policy->clear_requested = false;

	for (int cls = 0; cls < BMS_PROT_CLASS_COUNT; ++cls) {
		prot_update_class(policy, (BmsProtClass)cls, sample, now_ms, clear);
	}
	prot_update_afe(policy, afe_read_ok, afe_status, now_ms, clear);

	fault_bits = policy->latched_bits;

	/*
	 * NTC 失效保护(规格 §9):参与保护的温度通道开路/短路时,温度不可知,
	 * 直接禁充(低温充电风险最高)。安全动作即时生效,不做去抖;放电保留,
	 * 避免因单点传感器故障把电池包彻底封死。这个位不进 latched_bits 状态机,
	 * 每帧按当前采样重算——传感器恢复即自动解除。
	 */
	if (any_prot_temp_untrusted(sample)) {
		fault_bits |= BMS_FAULT_TEMP_SENSOR;
	}

	out->timestamp_ms = now_ms;
	out->fault_bits = fault_bits;
	out->afe_status = policy->last_afe_status;
	out->latched = (fault_bits != 0U);
	out->charge_allowed = (fault_bits &
			       (BMS_FAULT_CELL_OV | BMS_FAULT_CHARGE_OC |
				BMS_FAULT_CHG_OT | BMS_FAULT_CHG_UT |
				BMS_FAULT_AFE_ALERT | BMS_FAULT_AFE_COMM |
				BMS_FAULT_TEMP_SENSOR)) == 0U;
	out->discharge_allowed = (fault_bits &
				  (BMS_FAULT_CELL_UV | BMS_FAULT_DISCHARGE_OC |
				   BMS_FAULT_DSG_OT | BMS_FAULT_DSG_UT |
				   BMS_FAULT_AFE_ALERT | BMS_FAULT_AFE_COMM)) == 0U;
}
