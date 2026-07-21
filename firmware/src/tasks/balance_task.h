#ifndef BALANCE_TASK_H
#define BALANCE_TASK_H

#include "core/bms_heartbeat.h"
#include "core/bms_state_store.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

int balance_task_bind(const struct device *afe,
		      BmsStateStore *state_store,
		      struct k_event *events,
		      uint32_t wait_events,
		      BmsHeartbeatTable *heartbeat);
void balance_task_entry(void *arg0, void *arg1, void *arg2);

#endif
