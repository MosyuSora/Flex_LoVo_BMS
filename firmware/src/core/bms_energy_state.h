#ifndef BMS_ENERGY_STATE_H
#define BMS_ENERGY_STATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 能量状态,字段与单位对齐 04-03/TC-04 规格:
 * SOC/SOH/可信度用 0.1%(1000 = 100.0%),容量 mAh。
 * qmax 是容量账(第一版 = 额定,后续 SOH 学习修正);remain_mah 用有符号
 * 便于积分瞬态越界后再限幅;valid 表示当前估计是否可信。
 */
typedef struct BmsEnergyState {
	uint32_t version;
	uint32_t timestamp_ms;      /* 最近一次成功更新的采样时间 */
	uint16_t soc_0p1pct;        /* 0.1%,1000 = 100.0% */
	uint16_t soh_0p1pct;        /* 第一版 = qmax/额定,占位 1000 */
	uint16_t confidence_0p1pct; /* SOC 可信度,0.1% */
	uint16_t charge_limit_ma;   /* SOP 允许充电电流 */
	uint16_t discharge_limit_ma;/* SOP 允许放电电流 */
	uint16_t sop_charge_w;
	uint16_t sop_discharge_w;
	int32_t remain_mah;         /* 剩余容量 */
	uint32_t qmax_mah;          /* 容量账(积分分母) */
	uint32_t efc_0p01;          /* 等效满充次数,0.01 次 */
	bool initialized;           /* 是否已用 OCV 引导过一次 */
	bool valid;                 /* 当前 SOC 估计是否可信 */
} BmsEnergyState;

#endif
