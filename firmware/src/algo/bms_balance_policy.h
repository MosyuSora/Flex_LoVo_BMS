#ifndef BMS_BALANCE_POLICY_H
#define BMS_BALANCE_POLICY_H

#include "core/bms_balance_state.h"
#include "core/bms_fault_state.h"
#include "core/bms_sample_snapshot.h"

#include <stdint.h>

/*
 * 被动均衡策略(纯函数):
 * 输入快照/故障/上一轮 mask,输出自然电芯位图(bit0 = 第 1 节)。
 * 寄存器拆分是 AFE 驱动的事,这里只谈"哪些电芯该放电"。
 */
uint16_t bms_balance_policy_select_mask(const BmsSampleSnapshot *sample,
					const BmsFaultState *fault,
					uint16_t previous_mask,
					BmsBalanceState *state_out);

#endif
