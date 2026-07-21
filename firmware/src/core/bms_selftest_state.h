#ifndef BMS_SELFTEST_STATE_H
#define BMS_SELFTEST_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct BmsSelftestState {
	uint32_t version;
	uint32_t timestamp_ms;
	uint32_t heartbeat_stale_mask; /* 位 = BmsHeartbeatSlot,置位表示超时 */
	bool boot_ok;                  /* 上电自检(AFE 通信)结果 */
	bool watchdog_enabled;
} BmsSelftestState;

#endif
