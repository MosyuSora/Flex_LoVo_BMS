#include "sensor/bms_ntc.h"

#include <errno.h>
#include <zephyr/sys/util.h>

/*
 * 板级分压 + 传感器特性(换 NTC 型号 / 分压网络只改这里,不动 AFE 芯片驱动)。
 * Based on docs/board-facts: 10k pull-up to 3.3V,
 * NTC 10k@25C B=3380.
 */
#define BMS_NTC_PULLUP_OHM 10000U
#define BMS_NTC_VREF_MV 3300U
/*
 * 开短路诊断(TS 分压 = VREF * Rntc/(Rpull+Rntc)):
 * NTC 短路 -> Rntc->0 -> 分压->0;NTC 开路 -> Rntc->inf -> 分压->VREF。
 */
#define BMS_NTC_SHORT_MV 100U  /* 分压 <= 此值:判短路 */
#define BMS_NTC_OPEN_MV 3200U  /* 分压 >= 此值(逼近 VREF):判开路 */

/*
 * NTC 阻值-温度表:R(T) = R25 * exp(B * (1/T - 1/298.15)),R25=10k、B=3380,
 * 在 -20..80C 按 5C 步长离线制表。片上纯整数插值,不引入 log/浮点。
 * 温度单位 0.01C。
 */
struct ntc_point {
	int16_t cdeg;
	uint32_t ohm;
};

static const struct ntc_point NTC_10K_B3380[] = {
	{ -2000, 75022U }, { -1500, 57926U }, { -1000, 45168U }, {  -500, 35548U },
	{     0, 28224U }, {   500, 22595U }, {  1000, 18232U }, {  1500, 14820U },
	{  2000, 12133U }, {  2500, 10000U }, {  3000,  8295U }, {  3500,  6923U },
	{  4000,  5811U }, {  4500,  4904U }, {  5000,  4160U }, {  5500,  3548U },
	{  6000,  3040U }, {  6500,  2616U }, {  7000,  2262U }, {  7500,  1963U },
	{  8000,  1711U },
};

static int16_t ntc_ohm_to_cdeg(uint32_t ohm)
{
	const size_t last = ARRAY_SIZE(NTC_10K_B3380) - 1U;

	if (ohm >= NTC_10K_B3380[0].ohm) {
		return NTC_10K_B3380[0].cdeg;
	}
	if (ohm <= NTC_10K_B3380[last].ohm) {
		return NTC_10K_B3380[last].cdeg;
	}
	for (size_t i = 0U; i < last; ++i) {
		const struct ntc_point *cold = &NTC_10K_B3380[i];
		const struct ntc_point *hot = &NTC_10K_B3380[i + 1U];

		if ((ohm <= cold->ohm) && (ohm >= hot->ohm)) {
			uint32_t span_ohm = cold->ohm - hot->ohm;
			uint32_t delta_ohm = cold->ohm - ohm;
			int32_t span_cdeg = hot->cdeg - cold->cdeg;

			return (int16_t)(cold->cdeg +
					 ((int32_t)delta_ohm * span_cdeg) /
						 (int32_t)span_ohm);
		}
	}
	return 0;
}

int bms_ntc_mv_to_cdeg(uint16_t ts_mv, int16_t *cdeg_out)
{
	uint32_t ohm;

	if (cdeg_out == NULL) {
		return -EINVAL;
	}
	/* 开短路:温度不可知,清 0 并报 -ERANGE,让上层清 valid 位。 */
	if ((ts_mv <= BMS_NTC_SHORT_MV) || (ts_mv >= BMS_NTC_OPEN_MV)) {
		*cdeg_out = 0;
		return -ERANGE;
	}
	/* OPEN_MV < VREF,故分母 (VREF - ts_mv) 恒 > 0,无除零。 */
	ohm = (BMS_NTC_PULLUP_OHM * (uint32_t)ts_mv) / (BMS_NTC_VREF_MV - ts_mv);
	*cdeg_out = ntc_ohm_to_cdeg(ohm);
	return 0;
}
