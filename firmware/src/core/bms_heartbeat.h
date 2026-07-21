#ifndef BMS_HEARTBEAT_H
#define BMS_HEARTBEAT_H

#include <stdint.h>

/*
 * 任务心跳表:每个任务一个槽位,循环里打点;辅助任务统一巡检。
 *
 * 无锁设计:槽位是 32 位对齐字,Cortex-M 上字写入天然原子;
 * 巡检读到的最坏情况是"晚一拍的时间戳",不影响超时判断的正确性。
 */
typedef enum BmsHeartbeatSlot {
	BMS_HB_SLOT_SAMPLE = 0,
	BMS_HB_SLOT_FAULT,
	BMS_HB_SLOT_CHARGE,
	BMS_HB_SLOT_BALANCE,
	BMS_HB_SLOT_SOC,
	BMS_HB_SLOT_CAN,
	BMS_HB_SLOT_DIAG,
	BMS_HB_SLOT_COUNT,
} BmsHeartbeatSlot;

typedef struct BmsHeartbeatTable {
	volatile uint32_t beat_ms[BMS_HB_SLOT_COUNT];
} BmsHeartbeatTable;

void bms_heartbeat_init(BmsHeartbeatTable *table);
void bms_heartbeat_beat(BmsHeartbeatTable *table, BmsHeartbeatSlot slot);

/*
 * 返回超时槽位位图(bit = BmsHeartbeatSlot)。
 * expected_mask 由板级装配给出:只巡检真正启用的任务。
 */
uint32_t bms_heartbeat_stale_mask(const BmsHeartbeatTable *table,
				  uint32_t now_ms,
				  uint32_t timeout_ms,
				  uint32_t expected_mask);

#endif
