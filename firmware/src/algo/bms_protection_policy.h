#ifndef BMS_PROTECTION_POLICY_H
#define BMS_PROTECTION_POLICY_H

#include "core/bms_fault_state.h"
#include "core/bms_sample_snapshot.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * 保护策略对象:去抖 + 回差 + 恢复持续时间的三段式状态机。
 *
 * - 触发:越限持续 >= trip 时间才置位(过流 100ms,其余 300ms)。
 * - 恢复:回到回差以内持续 >= recover 时间才解除。
 * - AFE 粘滞位与通信失败单独走各自的状态机。
 *
 * 纯算法对象:不做任何 I/O;同一份输入永远得到同一份输出,
 * 可在主机侧单测。
 */
typedef enum BmsProtClass {
	BMS_PROT_OV = 0,
	BMS_PROT_UV,
	BMS_PROT_DSG_OC,
	BMS_PROT_CHG_OC,
	BMS_PROT_CHG_OT,
	BMS_PROT_CHG_UT,
	BMS_PROT_DSG_OT,
	BMS_PROT_DSG_UT,
	BMS_PROT_CLASS_COUNT,
} BmsProtClass;

typedef struct BmsProtectionPolicy {
	bool trip_pending[BMS_PROT_CLASS_COUNT];
	uint32_t trip_since_ms[BMS_PROT_CLASS_COUNT];
	bool recover_pending[BMS_PROT_CLASS_COUNT];
	uint32_t recover_since_ms[BMS_PROT_CLASS_COUNT];
	uint32_t latched_bits;
	uint16_t last_afe_status;
	bool afe_recover_pending;
	uint32_t afe_recover_since_ms;
	uint8_t comm_fail_count;
	bool comm_recover_pending;
	uint32_t comm_recover_since_ms;
	bool clear_requested;
} BmsProtectionPolicy;

void bms_protection_policy_init(BmsProtectionPolicy *policy);

/* 外部命令(CAN 清障):下一次 update 时对已消失的故障立即解除 */
void bms_protection_policy_request_clear(BmsProtectionPolicy *policy);

/*
 * now_ms 由调用方显式传入(任务用 k_uptime,单测用假时钟):
 * AFE 失联时采样快照会停摆,计时不能依赖快照里的时间戳。
 */
void bms_protection_policy_update(BmsProtectionPolicy *policy,
				  const BmsSampleSnapshot *sample,
				  bool afe_read_ok,
				  uint16_t afe_status,
				  uint32_t now_ms,
				  BmsFaultState *out);

#endif
