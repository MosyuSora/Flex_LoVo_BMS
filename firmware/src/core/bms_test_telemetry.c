#include "core/bms_test_telemetry.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/debug/thread_analyzer.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BMS_TEST_TELEMETRY_PERIOD_MS 1000U

static uint32_t s_sample_timestamp_ms;
static uint32_t s_previous_sample_ms;
static uint32_t s_thread_count;
static uint32_t s_stack_used_total;
static uint32_t s_stack_size_total;
static uint32_t s_cpu_utilization_total;

static void telemetry_thread_callback(struct thread_analyzer_info *info)
{
	uint32_t used = (uint32_t)info->stack_used;
	uint32_t size = (uint32_t)info->stack_size;
	uint32_t remaining = size - used;
	uint32_t stack_percent = (size != 0U) ? ((used * 100U) / size) : 0U;
	uint32_t cpu_percent = 0U;
	int priority = 0;

#ifdef CONFIG_THREAD_RUNTIME_STATS
	cpu_percent = info->utilization;
#endif
#ifdef CONFIG_THREAD_ANALYZER_PRINT_THREAD_PRIORITY
	priority = info->prio;
#endif

	s_thread_count++;
	s_stack_used_total += used;
	s_stack_size_total += size;
	if (strncmp(info->name, "idle", 4) != 0) {
		s_cpu_utilization_total += cpu_percent;
	}

	printk("BMS_THREAD_STACK {\"timestamp_ms\":%u,\"thread\":\"%s\","
	       "\"used_bytes\":%u,\"size_bytes\":%u,\"remaining_bytes\":%u,"
	       "\"stack_utilization_percent\":%u,\"cpu_utilization_percent\":%u,"
	       "\"priority\":%d}\n",
	       s_sample_timestamp_ms, info->name, used, size, remaining,
	       stack_percent, cpu_percent, priority);
}

void bms_test_telemetry_sample(uint32_t now_ms)
{
	if ((s_previous_sample_ms != 0U) &&
	    ((uint32_t)(now_ms - s_previous_sample_ms) < BMS_TEST_TELEMETRY_PERIOD_MS)) {
		return;
	}

	s_previous_sample_ms = now_ms;
	s_sample_timestamp_ms = now_ms;
	s_thread_count = 0U;
	s_stack_used_total = 0U;
	s_stack_size_total = 0U;
	s_cpu_utilization_total = 0U;

	thread_analyzer_run(telemetry_thread_callback, 0U);
	printk("BMS_SCHED {\"timestamp_ms\":%u,\"thread_count\":%u,"
	       "\"stack_used_total_bytes\":%u,\"stack_reserved_total_bytes\":%u,"
	       "\"cpu_utilization_percent\":%u,\"runtime_stats_available\":%s}\n",
	       now_ms, s_thread_count, s_stack_used_total, s_stack_size_total,
	       s_cpu_utilization_total,
#ifdef CONFIG_THREAD_RUNTIME_STATS
	       "true"
#else
	       "false"
#endif
	);
}

void bms_test_telemetry_sample_timing(uint32_t start_ms, uint32_t end_ms,
					      bool success)
{
	printk("BMS_SAMPLE_TIMING {\"start_ms\":%u,\"end_ms\":%u,"
	       "\"duration_ms\":%u,\"success\":%u}\n",
	       start_ms, end_ms, end_ms - start_ms, success ? 1U : 0U);
}

void bms_test_telemetry_deadline_miss(const char *task,
				      uint32_t now_ms,
				      uint32_t lateness_ms,
				      uint32_t skipped_periods)
{
	printk("BMS_SCHED_OVERRUN {\"timestamp_ms\":%u,\"task\":\"%s\","
	       "\"lateness_ms\":%u,\"skipped_periods\":%u}\n",
	       now_ms, task, lateness_ms, skipped_periods);
}

void bms_test_telemetry_led(uint32_t now_ms, bool fault_latched, int state)
{
	printk("BMS_LED_STATE {\"timestamp_ms\":%u,\"fault\":%u,\"state\":%d}\n",
	       now_ms, fault_latched ? 1U : 0U, state);
}

void bms_test_telemetry_watchdog(uint32_t now_ms,
				 bool setup_ok,
				 uint32_t feed_count,
				 uint32_t max_gap_ms)
{
	printk("BMS_WDT_STATE {\"timestamp_ms\":%u,\"setup_ok\":%u,"
	       "\"feed_count\":%u,\"max_gap_ms\":%u}\n",
	       now_ms, setup_ok ? 1U : 0U, feed_count, max_gap_ms);
}

void bms_test_telemetry_can_tx(uint32_t now_ms, uint32_t frame_id,
				       uint8_t dlc, const uint8_t *data, int result)
{
	const uint8_t *bytes = (data != NULL) ? data : (const uint8_t[8]){0};

	/* Keep the payload in test telemetry so the software trace can be checked
	 * against a bus-level CAN capture. This code is only built for telemetry. */
	printk("BMS_CAN_TX {\"timestamp_ms\":%u,\"id\":%u,\"dlc\":%u,"
	       "\"data\":\"%02x %02x %02x %02x %02x %02x %02x %02x\","
	       "\"result\":%d}\n",
	       now_ms, frame_id, dlc, bytes[0], bytes[1], bytes[2], bytes[3],
	       bytes[4], bytes[5], bytes[6], bytes[7], result);
}

void bms_test_telemetry_can_state(uint32_t now_ms, int setup_result,
				  int filter_result)
{
	printk("BMS_CAN_STATE {\"timestamp_ms\":%u,\"setup_result\":%d,"
	       "\"filter_result\":%d}\n",
	       now_ms, setup_result, filter_result);
}
