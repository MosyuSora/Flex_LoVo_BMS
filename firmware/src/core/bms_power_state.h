#ifndef BMS_POWER_STATE_H
#define BMS_POWER_STATE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 枚举数值是对外契约(UART 诊断与测试台断言按数值判断),不要重排。
 */
typedef enum BmsPowerMode {
	BMS_POWER_MODE_IDLE = 0,
	BMS_POWER_MODE_CHARGING = 1,
	BMS_POWER_MODE_DISCHARGING = 2,
	BMS_POWER_MODE_FAULT = 3,
} BmsPowerMode;

typedef struct BmsPowerState {
	uint32_t version;
	uint32_t timestamp_ms;
	BmsPowerMode mode;
	bool chg_enabled;  /* CHG FET 实际状态(读回) */
	bool dsg_enabled;  /* DSG FET 实际状态(读回) */
	bool charge_allowed;
	bool discharge_allowed;
} BmsPowerState;

#endif
