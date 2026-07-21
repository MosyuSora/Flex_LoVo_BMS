#include "tasks/balance_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"

/* ---- 任务自注册(见 config/app_task.h) ---- */
#if BMS_APP_ENABLE_BALANCE
static int balance_bind_wrapper(void)
{
	return balance_task_bind(app_afe, &app_state_store, &app_events,
				 BMS_EVT_SAMPLE_BALANCE, &app_heartbeat);
}
APP_TASK_REGISTER(balance, balance_bind_wrapper, balance_task_entry,
		  BMS_APP_BALANCE_PRIORITY, BMS_APP_BALANCE_STACK_SIZE);
#endif

#include "afe/bms_afe.h"
#include "algo/bms_balance_policy.h"

static const struct device *s_afe;
static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_wait_events;
static BmsHeartbeatTable *s_heartbeat;

int balance_task_bind(const struct device *afe,
		      BmsStateStore *state_store,
		      struct k_event *events,
		      uint32_t wait_events,
		      BmsHeartbeatTable *heartbeat)
{
	if ((afe == NULL) || (state_store == NULL) || (events == NULL) ||
	    (wait_events == 0U)) {
		return -1;
	}

	s_afe = afe;
	s_state_store = state_store;
	s_events = events;
	s_wait_events = wait_events;
	s_heartbeat = heartbeat;
	return 0;
}

void balance_task_entry(void *arg0, void *arg1, void *arg2)
{
	uint32_t last_sample_version = 0U;
	uint16_t previous_mask = 0U;

	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	for (;;) {
		BmsSampleSnapshot sample;
		BmsFaultState fault;
		BmsBalanceState balance = {0};

		(void)k_event_wait(s_events, s_wait_events, false, K_FOREVER);
		k_event_clear(s_events, s_wait_events);
		bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_BALANCE);

		if (bms_state_store_read_sample(s_state_store, &sample) != 0) {
			continue;
		}
		if (sample.version == last_sample_version) {
			continue;
		}
		last_sample_version = sample.version;

		(void)bms_state_store_read_fault(s_state_store, &fault);
		balance.timestamp_ms = k_uptime_get_32();

		/* 策略输出自然电芯位图;寄存器拆分在 BQ 驱动里完成 */
		balance.requested_mask = bms_balance_policy_select_mask(
			&sample, &fault, previous_mask, &balance);
		if (bms_afe_set_balance_mask(s_afe, balance.requested_mask) == 0) {
			balance.applied_mask = balance.requested_mask;
			previous_mask = balance.applied_mask;
		} else {
			balance.applied_mask = previous_mask;
		}

		(void)bms_state_store_write_balance(s_state_store, &balance);
	}
}
