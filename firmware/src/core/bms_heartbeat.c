#include "core/bms_heartbeat.h"

#include <string.h>
#include <zephyr/kernel.h>

void bms_heartbeat_init(BmsHeartbeatTable *table)
{
	if (table == NULL) {
		return;
	}

	/*
	 * 初始值填当前时间而不是 0:任务线程可能比辅助任务晚起,
	 * 否则第一轮巡检会把还没跑起来的任务误判为超时。
	 */
	uint32_t now = k_uptime_get_32();

	for (int i = 0; i < BMS_HB_SLOT_COUNT; ++i) {
		table->beat_ms[i] = now;
	}
}

void bms_heartbeat_beat(BmsHeartbeatTable *table, BmsHeartbeatSlot slot)
{
	if ((table == NULL) || (slot >= BMS_HB_SLOT_COUNT)) {
		return;
	}

	table->beat_ms[slot] = k_uptime_get_32();
}

uint32_t bms_heartbeat_stale_mask(const BmsHeartbeatTable *table,
				  uint32_t now_ms,
				  uint32_t timeout_ms,
				  uint32_t expected_mask)
{
	uint32_t stale = 0U;

	if (table == NULL) {
		return 0U;
	}

	for (int i = 0; i < BMS_HB_SLOT_COUNT; ++i) {
		if ((expected_mask & (1UL << i)) == 0U) {
			continue;
		}
		/* 无符号减法对 uptime 回绕安全 */
		if ((now_ms - table->beat_ms[i]) > timeout_ms) {
			stale |= (1UL << i);
		}
	}

	return stale;
}
