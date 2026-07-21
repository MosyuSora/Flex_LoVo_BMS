#ifndef BMS_FAULT_STATE_H
#define BMS_FAULT_STATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 软件保护故障位。充/放电温度窗口分开(板级事实:充电 0..55C,
 * 放电 -20..70C),因此温度故障有四个方向位。
 */
#define BMS_FAULT_CELL_OV (1UL << 0)
#define BMS_FAULT_CELL_UV (1UL << 1)
#define BMS_FAULT_DISCHARGE_OC (1UL << 2)
#define BMS_FAULT_CHARGE_OC (1UL << 3)
#define BMS_FAULT_CHG_OT (1UL << 4)
#define BMS_FAULT_CHG_UT (1UL << 5)
#define BMS_FAULT_DSG_OT (1UL << 6)
#define BMS_FAULT_DSG_UT (1UL << 7)
#define BMS_FAULT_AFE_ALERT (1UL << 8)
#define BMS_FAULT_AFE_COMM (1UL << 9)
#define BMS_FAULT_SAMPLE_INVALID (1UL << 10)
#define BMS_FAULT_TEMP_SENSOR (1UL << 11) /* 参与保护的 NTC 开路/短路 */

typedef struct BmsFaultState {
	uint32_t version;
	uint32_t timestamp_ms;
	uint32_t fault_bits;
	uint16_t afe_status; /* 最近一次读到的 BQ SYS_STAT 原始值 */
	bool latched;        /* 当前存在未恢复的故障 */
	bool charge_allowed;
	bool discharge_allowed;
} BmsFaultState;

#endif
