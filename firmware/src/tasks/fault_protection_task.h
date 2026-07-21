#ifndef FAULT_PROTECTION_TASK_H
#define FAULT_PROTECTION_TASK_H

#include "core/bms_heartbeat.h"
#include "algo/bms_protection_policy.h"
#include "core/bms_state_store.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

/*
 * FaultProtectionTask:唯一的 CHG/DSG FET 仲裁者。
 * wait_events = 自己的采样事件位;notify_mask = 故障变化时要通知谁
 * (由装配根决定)。cmd_q 送入外部命令(如 CAN 清障)。
 */
int fault_protection_task_bind(const struct device *afe,
			       BmsStateStore *state_store,
			       struct k_event *events,
			       uint32_t wait_events,
			       uint32_t notify_mask,
			       BmsProtectionPolicy *policy,
			       struct k_msgq *cmd_q,
			       BmsHeartbeatTable *heartbeat);
void fault_protection_task_entry(void *arg0, void *arg1, void *arg2);

#endif
