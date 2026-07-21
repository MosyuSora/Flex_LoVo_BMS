#ifndef CHARGE_STATE_TASK_H
#define CHARGE_STATE_TASK_H

#include "algo/bms_charge_policy.h"
#include "core/bms_heartbeat.h"
#include "core/bms_state_store.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

/*
 * ChargeStateTask:观察者与状态分类器。
 * 只记录保护任务执行 CHG/DSG 策略后的电池包模式,不抢仲裁权。
 */
int charge_state_task_bind(const struct device *afe,
			   BmsStateStore *state_store,
			   struct k_event *events,
			   uint32_t wait_events,
			   BmsChargePolicy *policy,
			   BmsHeartbeatTable *heartbeat);
void charge_state_task_entry(void *arg0, void *arg1, void *arg2);

#endif
