#include "algo/bms_energy_estimator.h"

#include "config/appconfig.h"

#include <stddef.h>
#include <stdlib.h>
#include <zephyr/sys/util.h>

/*
 * 三元锂通用 OCV-SOC 锚点表,SOC 单位 0.1%(0..1000)。
 * 换电芯体系时整表替换,并同步校核测试场景期望值。
 */
typedef struct OcvPoint {
	uint16_t mv;
	uint16_t soc_0p1pct;
} OcvPoint;

static const OcvPoint OCV_TABLE[] = {
	{ 3000U, 0U },
	{ 3300U, 100U },
	{ 3500U, 200U },
	{ 3600U, 300U },
	{ 3700U, 400U },
	{ 3780U, 500U },
	{ 3860U, 600U },
	{ 3950U, 700U },
	{ 4050U, 800U },
	{ 4150U, 900U },
	{ 4200U, 1000U },
};

static uint16_t soc_from_ocv(uint16_t mv)
{
	if (mv <= OCV_TABLE[0].mv) {
		return OCV_TABLE[0].soc_0p1pct;
	}

	for (size_t i = 1U; i < ARRAY_SIZE(OCV_TABLE); ++i) {
		const OcvPoint *lo = &OCV_TABLE[i - 1U];
		const OcvPoint *hi = &OCV_TABLE[i];

		if (mv <= hi->mv) {
			uint32_t span_mv = hi->mv - lo->mv;
			uint32_t delta_mv = mv - lo->mv;
			uint32_t span_soc = hi->soc_0p1pct - lo->soc_0p1pct;

			return (uint16_t)(lo->soc_0p1pct +
					  (delta_mv * span_soc) / span_mv);
		}
	}

	return 1000U;
}

static uint16_t min_cell_mv(const BmsSampleSnapshot *sample)
{
	uint16_t min_mv = UINT16_MAX;

	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		min_mv = MIN(min_mv, sample->cell_mv[i]);
	}
	return min_mv;
}

static uint16_t max_cell_mv(const BmsSampleSnapshot *sample)
{
	uint16_t max_mv = 0U;

	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		max_mv = MAX(max_mv, sample->cell_mv[i]);
	}
	return max_mv;
}

/* 平滑校准:向目标按千分比小步靠拢,避免 SOC 显示突跳 */
static uint16_t smooth_toward(uint16_t cur, uint16_t target, uint16_t permille)
{
	return (uint16_t)(((uint32_t)(1000U - permille) * cur +
			   (uint32_t)permille * target) /
			  1000U);
}

static uint32_t soc_to_remain_mah(uint32_t qmax_mah, uint16_t soc_0p1pct)
{
	return (uint32_t)((uint64_t)qmax_mah * soc_0p1pct / 1000U);
}

/*
 * 采样有效性门控(规格 §9.3):电压/电流无效时冻结,不能拿坏数据继续积分。
 * 本工程用 version==0(尚无真实采样)和快照无效/AFE 失联故障位表达。
 */
static bool sample_is_valid(const BmsSampleSnapshot *sample, const BmsFaultState *fault)
{
	if (sample->version == 0U) {
		return false;
	}
	if ((fault->fault_bits & (BMS_FAULT_SAMPLE_INVALID | BMS_FAULT_AFE_COMM)) != 0U) {
		return false;
	}
	return true;
}

/*
 * SOP 规则表(SOC 单位 0.1%):高 SOC/高压 → 收充电,低 SOC/低压 → 收放电,
 * 温度不适再统一收紧。规则值属于业务标定,不是物理推导。
 */
static uint16_t sop_charge_limit(const BmsSampleSnapshot *sample, uint16_t soc_0p1pct)
{
	uint16_t limit = BMS_APP_SOP_MAX_CHARGE_MA;
	uint16_t vmax = max_cell_mv(sample);

	if ((soc_0p1pct >= 950U) || (vmax >= 4150U)) {
		limit = MIN(limit, 1000U);
	} else if ((soc_0p1pct >= 900U) || (vmax >= 4100U)) {
		limit = MIN(limit, 3000U);
	}

	for (size_t i = 0U; i < BMS_APP_PROT_TEMP_CHANNELS; ++i) {
		if ((sample->temp_valid & (uint8_t)(1U << i)) == 0U) {
			continue; /* 不可信通道不参与降额,交给保护的传感器故障分支 */
		}
		if ((sample->temperature_cdeg[i] < 0) ||
		    (sample->temperature_cdeg[i] > 4500)) {
			limit = MIN(limit, 3000U);
		}
	}

	return limit;
}

static uint16_t sop_discharge_limit(const BmsSampleSnapshot *sample, uint16_t soc_0p1pct)
{
	uint16_t limit = BMS_APP_SOP_MAX_DISCHARGE_MA;
	uint16_t vmin = min_cell_mv(sample);

	if ((soc_0p1pct <= 50U) || (vmin <= 3100U)) {
		limit = MIN(limit, 2000U);
	} else if ((soc_0p1pct <= 100U) || (vmin <= 3300U)) {
		limit = MIN(limit, 6000U);
	}

	for (size_t i = 0U; i < BMS_APP_PROT_TEMP_CHANNELS; ++i) {
		if ((sample->temp_valid & (uint8_t)(1U << i)) == 0U) {
			continue;
		}
		if ((sample->temperature_cdeg[i] < -1000) ||
		    (sample->temperature_cdeg[i] > 5500)) {
			limit = MIN(limit, 5000U);
		}
	}

	return limit;
}

/* 满充/放空硬边界重标定(规格 §9.2):用最小单体保证全包到位或见底 */
static void apply_boundary_correction(BmsEnergyState *energy, uint16_t vmin)
{
	if (vmin >= BMS_APP_SOC_FULL_MIN_CELL_MV) {
		energy->soc_0p1pct = smooth_toward(energy->soc_0p1pct, 1000U,
						   BMS_APP_SOC_BOUND_BLEND_PERMILLE);
		energy->remain_mah =
			(int32_t)soc_to_remain_mah(energy->qmax_mah, energy->soc_0p1pct);
	} else if (vmin <= BMS_APP_SOC_EMPTY_MIN_CELL_MV) {
		energy->soc_0p1pct = smooth_toward(energy->soc_0p1pct, 0U,
						   BMS_APP_SOC_BOUND_BLEND_PERMILLE);
		energy->remain_mah =
			(int32_t)soc_to_remain_mah(energy->qmax_mah, energy->soc_0p1pct);
	}
}

void bms_energy_estimator_update(const BmsSampleSnapshot *sample,
				 const BmsFaultState *fault,
				 BmsEnergyState *energy)
{
	uint16_t vmin;

	if ((sample == NULL) || (fault == NULL) || (energy == NULL)) {
		return;
	}

	/* 冻结态:采样无效 → 降可信度,保持上次 SOC,不积分不校准,SOP 保守清零 */
	if (!sample_is_valid(sample, fault)) {
		energy->confidence_0p1pct = BMS_APP_SOC_CONF_FROZEN;
		energy->charge_limit_ma = 0U;
		energy->discharge_limit_ma = 0U;
		energy->sop_charge_w = 0U;
		energy->sop_discharge_w = 0U;
		return;
	}

	vmin = min_cell_mv(sample);

	if (!energy->initialized) {
		/* 初始化态:用最小单体 OCV 引导 SOC 与容量账 */
		uint16_t soc = soc_from_ocv(vmin);

		energy->qmax_mah = BMS_APP_RATED_CAPACITY_MAH;
		energy->soc_0p1pct = soc;
		energy->remain_mah = (int32_t)soc_to_remain_mah(energy->qmax_mah, soc);
		energy->soh_0p1pct = 1000U; /* qmax = 额定 → 100.0%,占位 */
		energy->efc_0p01 = 0U;
		energy->confidence_0p1pct = BMS_APP_SOC_CONF_CALIBRATED;
		energy->initialized = true;
		energy->valid = true;
	} else if (abs(sample->current_ma) <= BMS_APP_OCV_REST_CURRENT_MA) {
		/* 校准态:静置小电流,向 OCV(min) 平滑校准,不积分 */
		uint16_t ocv_soc = soc_from_ocv(vmin);

		energy->soc_0p1pct = smooth_toward(energy->soc_0p1pct, ocv_soc,
						   BMS_APP_OCV_REST_BLEND_PERMILLE);
		energy->remain_mah =
			(int32_t)soc_to_remain_mah(energy->qmax_mah, energy->soc_0p1pct);
		energy->confidence_0p1pct = BMS_APP_SOC_CONF_CALIBRATED;
	} else {
		/* 积分态:库仑积分(放电为正 → 剩余减少) */
		uint32_t dt_ms = sample->timestamp_ms - energy->timestamp_ms;

		if (dt_ms <= BMS_APP_SOC_MAX_INTEGRATE_DT_MS) {
			int64_t delta_mah = ((int64_t)sample->current_ma *
					     (int64_t)dt_ms) /
					    3600000LL;
			int64_t remain = (int64_t)energy->remain_mah - delta_mah;

			remain = CLAMP(remain, 0, (int64_t)energy->qmax_mah);
			energy->remain_mah = (int32_t)remain;
			energy->soc_0p1pct = (uint16_t)((uint64_t)energy->remain_mah *
							1000U / energy->qmax_mah);
			energy->efc_0p01 += (uint32_t)(((int64_t)abs(sample->current_ma) *
							dt_ms * 100LL) /
						       (2LL * (int64_t)energy->qmax_mah *
							3600000LL));
		}
		energy->confidence_0p1pct = BMS_APP_SOC_CONF_INTEGRATING;
	}

	apply_boundary_correction(energy, vmin);

	energy->soc_0p1pct = (uint16_t)CLAMP((int32_t)energy->soc_0p1pct, 0, 1000);
	energy->timestamp_ms = sample->timestamp_ms;

	/* SOP:故障锁存优先清零,否则按规则表限流 */
	if (fault->latched) {
		energy->charge_limit_ma = 0U;
		energy->discharge_limit_ma = 0U;
	} else {
		energy->charge_limit_ma = sop_charge_limit(sample, energy->soc_0p1pct);
		energy->discharge_limit_ma =
			sop_discharge_limit(sample, energy->soc_0p1pct);
	}

	energy->sop_charge_w = (uint16_t)(((uint32_t)sample->pack_mv *
					   energy->charge_limit_ma) /
					  1000000U);
	energy->sop_discharge_w = (uint16_t)(((uint32_t)sample->pack_mv *
					      energy->discharge_limit_ma) /
					     1000000U);
}
