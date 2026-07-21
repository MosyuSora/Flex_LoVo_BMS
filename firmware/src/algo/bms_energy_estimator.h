#ifndef BMS_ENERGY_ESTIMATOR_H
#define BMS_ENERGY_ESTIMATOR_H

#include "core/bms_energy_state.h"
#include "core/bms_fault_state.h"
#include "core/bms_sample_snapshot.h"

/*
 * 能量估算(第一版算法,04-02/04-03 口径):
 * - 首帧用 OCV 表初始化 SOC。
 * - 之后库仑积分;静置时向 OCV 小步融合,抑制积分漂移。
 * - SOP 用规则表限流;SOH 为低可信度占位。
 * 纯算法模块:只消费快照/故障/自身状态,不做 I/O。
 */
void bms_energy_estimator_update(const BmsSampleSnapshot *sample,
				 const BmsFaultState *fault,
				 BmsEnergyState *energy);

#endif
