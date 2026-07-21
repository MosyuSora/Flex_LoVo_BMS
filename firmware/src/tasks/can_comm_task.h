#ifndef CAN_COMM_TASK_H
#define CAN_COMM_TASK_H

#include "core/bms_heartbeat.h"
#include "core/bms_state_store.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>

/*
 * CanCommTask:按 vendor 协议周期广播 7 帧状态;
 * RX 侧接收项目扩展命令帧,解码后经 cmd_q 送保护任务。
 */
int can_comm_task_bind(const struct device *can_dev,
		       BmsStateStore *state_store,
		       struct k_event *events,
		       uint32_t wait_events,
		       struct k_msgq *cmd_q,
		       uint32_t tx_period_ms,
		       BmsHeartbeatTable *heartbeat);
void can_comm_task_entry(void *arg0, void *arg1, void *arg2);

#endif
