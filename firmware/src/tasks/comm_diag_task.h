#ifndef COMM_DIAG_TASK_H
#define COMM_DIAG_TASK_H

#include "core/bms_heartbeat.h"
#include "core/bms_state_store.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

/*
 * CommDiagTask:bring-up 期 UART 诊断,不属于最终七任务。
 * 直接用 Zephyr UART 驱动(uart 设备由装配根注入)。SNAP 输出格式是测试台
 * (Renode 冒烟)断言的契约,改字段名前必须同步
 * tests/renode/run_zephyr_uart_smoke.ps1。
 */
int comm_diag_task_bind(const struct device *uart,
			BmsStateStore *state_store,
			struct k_event *events,
			uint32_t wait_events,
			uint32_t event_wait_timeout_ms,
			BmsHeartbeatTable *heartbeat);
void comm_diag_task_entry(void *arg0, void *arg1, void *arg2);

#endif
