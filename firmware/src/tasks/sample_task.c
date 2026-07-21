#include "tasks/sample_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"

/* ---- 任务自注册(见 config/app_task.h) ---- */
static int sample_bind_wrapper(void)
{
	return sample_task_bind(app_afe, &app_state_store, &app_events,
				APP_SAMPLE_NOTIFY_MASK, BMS_APP_SAMPLE_PERIOD_MS,
				&app_heartbeat);
}
APP_TASK_REGISTER(sample, sample_bind_wrapper, sample_task_entry,
		  BMS_APP_SAMPLE_PRIORITY, BMS_APP_SAMPLE_STACK_SIZE);

#include "afe/bms_afe.h"
#include "sensor/bms_ntc.h"

#ifdef CONFIG_BMS_TEST_TELEMETRY
#include "core/bms_test_telemetry.h"
#endif

#include <string.h>

static const struct device *s_afe;
static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_notify_mask;
static uint32_t s_period_ms;
static BmsHeartbeatTable *s_heartbeat;

static int sample_task_read_snapshot(BmsSampleSnapshot *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->timestamp_ms = k_uptime_get_32();

	/*
	 * 快照是纯测量值,不回填任何算法输出(SOC 在 BmsEnergyState 里)。
	 */
	if (bms_afe_read_cells(s_afe, snapshot->cell_mv, BMS_SAMPLE_CELL_COUNT) != 0) {
		return -1;
	}
	if (bms_afe_read_pack_voltage(s_afe, &snapshot->pack_mv) != 0) {
		return -1;
	}
	if (bms_afe_read_current(s_afe, &snapshot->current_ma) != 0) {
		return -1;
	}

	/*
	 * 温度分两步:AFE 出 TS 分压 mV(芯片职责),bms_ntc 把 mV 换成温度并做
	 * 开短路诊断(传感器职责)。开短路的通道 valid 位清 0、温度写 0。
	 */
	uint16_t ts_mv[BMS_SAMPLE_TEMP_COUNT];

	if (bms_afe_read_thermistor_mv(s_afe, ts_mv, BMS_SAMPLE_TEMP_COUNT) != 0) {
		return -1;
	}
	for (size_t i = 0U; i < BMS_SAMPLE_TEMP_COUNT; ++i) {
		int16_t cdeg = 0;

		if (bms_ntc_mv_to_cdeg(ts_mv[i], &cdeg) == 0) {
			snapshot->temperature_cdeg[i] = cdeg;
			snapshot->temp_valid |= (uint8_t)(1U << i);
		} else {
			snapshot->temperature_cdeg[i] = 0;
		}
	}

	return 0;
}

static int sample_task_publish_once(void)
{
	BmsSampleSnapshot snapshot;

	/*
	 * 读失败就跳过本轮发布:消费者按版本号去重,不会消费旧数据;
	 * AFE 失联的判定由保护任务的独立超时路径负责,这里不重复报警。
	 */
	if (sample_task_read_snapshot(&snapshot) != 0) {
		return -1;
	}
	if (bms_state_store_write_sample(s_state_store, &snapshot) != 0) {
		return -1;
	}

	k_event_post(s_events, s_notify_mask);
	return 0;
}

int sample_task_bind(const struct device *afe,
		     BmsStateStore *state_store,
		     struct k_event *events,
		     uint32_t notify_mask,
		     uint32_t period_ms,
		     BmsHeartbeatTable *heartbeat)
{
	if ((afe == NULL) || (state_store == NULL) || (events == NULL) ||
	    (period_ms == 0U)) {
		return -1;
	}

	s_afe = afe;
	s_state_store = state_store;
	s_events = events;
	s_notify_mask = notify_mask;
	s_period_ms = period_ms;
	s_heartbeat = heartbeat;
	return 0;
}

void sample_task_entry(void *arg0, void *arg1, void *arg2)
{
	int64_t next_tick;

	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	/* 绝对时间调度:发布耗时不吃掉采样周期 */
	next_tick = k_uptime_get();
	for (;;) {
		int64_t now;
		uint32_t sample_start = k_uptime_get_32();
		int sample_result;

		sample_result = sample_task_publish_once();
	#ifdef CONFIG_BMS_TEST_TELEMETRY
		bms_test_telemetry_sample_timing(sample_start, k_uptime_get_32(),
						  sample_result == 0);
	#else
		ARG_UNUSED(sample_start);
		ARG_UNUSED(sample_result);
	#endif
		bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_SAMPLE);
		next_tick += s_period_ms;
		now = k_uptime_get();
		/* Exactly-on-deadline completion is schedulable; only late work is a miss. */
		if (now > next_tick) {
			uint32_t lateness_ms = (uint32_t)(now - next_tick);
			uint32_t skipped_periods = (lateness_ms / s_period_ms) + 1U;

#ifdef CONFIG_BMS_TEST_TELEMETRY
			bms_test_telemetry_deadline_miss("sample", (uint32_t)now,
						 lateness_ms, skipped_periods);
#endif
			next_tick += (int64_t)skipped_periods * s_period_ms;
		}
		k_sleep(K_TIMEOUT_ABS_MS(next_tick));
	}
}
