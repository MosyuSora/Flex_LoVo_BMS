#ifndef AUXILIARY_TASK_H
#define AUXILIARY_TASK_H

#include "core/bms_heartbeat.h"
#include "core/bms_state_store.h"

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

/*
 * AuxiliaryTask:心跳巡检、状态灯、看门狗、自检状态维护。
 * 状态灯由 bms_status_led 模块自持(aux 不再注入 LED 句柄);WDT 设备由装配根
 * 注入,wdt_dev 传 NULL 表示不启用看门狗。
 */
int auxiliary_task_bind(BmsStateStore *state_store,
			struct k_event *events,
			uint32_t wait_events,
			BmsHeartbeatTable *heartbeat,
			uint32_t heartbeat_expected_mask,
			const struct device *wdt_dev,
			bool boot_ok,
			uint32_t period_ms);
void auxiliary_task_entry(void *arg0, void *arg1, void *arg2);

#endif
