#ifndef SAMPLE_TASK_H
#define SAMPLE_TASK_H

#include "core/bms_heartbeat.h"
#include "core/bms_state_store.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

/*
 * SampleTask:唯一的周期性采样生产者。
 * notify_mask 由装配根(main.c)给出——任务不知道谁在消费。
 */
int sample_task_bind(const struct device *afe,
		     BmsStateStore *state_store,
		     struct k_event *events,
		     uint32_t notify_mask,
		     uint32_t period_ms,
		     BmsHeartbeatTable *heartbeat);
void sample_task_entry(void *arg0, void *arg1, void *arg2);

#endif
