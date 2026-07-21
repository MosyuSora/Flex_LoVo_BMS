#include "tasks/charge_state_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"

/* ---- 任务自注册(见 config/app_task.h) ---- */
#if BMS_APP_ENABLE_CHARGE_STATE
static int charge_state_bind_wrapper(void)
{
	return charge_state_task_bind(app_afe, &app_state_store, &app_events,
				      BMS_EVT_SAMPLE_CHARGE, &app_charge_policy,
				      &app_heartbeat);
}
APP_TASK_REGISTER(charge_state, charge_state_bind_wrapper,
		  charge_state_task_entry, BMS_APP_CHARGE_STATE_PRIORITY,
		  BMS_APP_CHARGE_STATE_STACK_SIZE);
#endif

#include "afe/bms_afe.h"

static const struct device *s_afe;
static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_wait_events;
static BmsChargePolicy *s_policy;
static BmsHeartbeatTable *s_heartbeat;

int charge_state_task_bind(const struct device *afe,
			   BmsStateStore *state_store,
			   struct k_event *events,
			   uint32_t wait_events,
			   BmsChargePolicy *policy,
			   BmsHeartbeatTable *heartbeat)
{
	if ((afe == NULL) || (state_store == NULL) || (events == NULL) ||
	    (wait_events == 0U) || (policy == NULL)) {
		return -1;
	}

	s_afe = afe;
	s_state_store = state_store;
	s_events = events;
	s_wait_events = wait_events;
	s_policy = policy;
	s_heartbeat = heartbeat;
	return 0;
}

void charge_state_task_entry(void *arg0, void *arg1, void *arg2)
{
	uint32_t last_sample_version = 0U;

	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	for (;;) {
		BmsSampleSnapshot sample;
		BmsFaultState fault;
		BmsPowerState power = {0};
		BmsSwitchState switches = {0};

		(void)k_event_wait(s_events, s_wait_events, false, K_FOREVER);
		k_event_clear(s_events, s_wait_events);
		bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_CHARGE);

		if (bms_state_store_read_sample(s_state_store, &sample) != 0) {
			continue;
		}
		if (sample.version == last_sample_version) {
			continue;
		}
		last_sample_version = sample.version;

		(void)bms_state_store_read_fault(s_state_store, &fault);
		(void)bms_afe_read_switch_state(s_afe, &switches);

		power.timestamp_ms = k_uptime_get_32();
		power.mode = bms_charge_policy_classify(s_policy,
							sample.current_ma,
							fault.latched);
		power.chg_enabled = switches.chg_enabled;
		power.dsg_enabled = switches.dsg_enabled;
		power.charge_allowed = fault.charge_allowed;
		power.discharge_allowed = fault.discharge_allowed;

		(void)bms_state_store_write_power(s_state_store, &power);
	}
}
