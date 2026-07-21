#ifndef BMS_TEST_TELEMETRY_H
#define BMS_TEST_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>

void bms_test_telemetry_sample(uint32_t now_ms);
void bms_test_telemetry_sample_timing(uint32_t start_ms, uint32_t end_ms,
					      bool success);
void bms_test_telemetry_deadline_miss(const char *task,
				      uint32_t now_ms,
				      uint32_t lateness_ms,
				      uint32_t skipped_periods);
void bms_test_telemetry_led(uint32_t now_ms, bool fault_latched, int state);
void bms_test_telemetry_watchdog(uint32_t now_ms,
					 bool setup_ok,
					 uint32_t feed_count,
					 uint32_t max_gap_ms);
void bms_test_telemetry_can_tx(uint32_t now_ms, uint32_t frame_id,
				       uint8_t dlc, const uint8_t *data, int result);
void bms_test_telemetry_can_state(uint32_t now_ms, int setup_result,
					  int filter_result);

#endif
