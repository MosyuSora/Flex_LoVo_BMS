#ifndef BMS_BALANCE_STATE_H
#define BMS_BALANCE_STATE_H

#include <stdint.h>

typedef struct BmsBalanceState {
	uint32_t version;
	uint32_t timestamp_ms;
	uint16_t requested_mask; /* 策略想开的通道(自然电芯位图,bit0=第1节) */
	uint16_t applied_mask;   /* 实际写入 AFE 成功的通道 */
	uint16_t min_cell_mv;
	uint16_t max_cell_mv;
	uint16_t delta_mv;
} BmsBalanceState;

#endif
