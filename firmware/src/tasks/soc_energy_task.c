#include "tasks/soc_energy_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"

/* ---- 任务自注册(见 config/app_task.h) ---- */
#if BMS_APP_ENABLE_SOC_ENERGY
static int soc_energy_bind_wrapper(void)
{
	return soc_energy_task_bind(&app_state_store, &app_events,
				    BMS_EVT_SAMPLE_SOC, &app_heartbeat);
}
APP_TASK_REGISTER(soc_energy, soc_energy_bind_wrapper, soc_energy_task_entry,
		  BMS_APP_SOC_ENERGY_PRIORITY, BMS_APP_SOC_ENERGY_STACK_SIZE);
#endif

#include "algo/bms_energy_estimator.h"

static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_wait_events;
static BmsHeartbeatTable *s_heartbeat;

int soc_energy_task_bind(BmsStateStore *state_store,
			 struct k_event *events,
			 uint32_t wait_events,
			 BmsHeartbeatTable *heartbeat)
{
	if ((state_store == NULL) || (events == NULL) || (wait_events == 0U)) {
		return -1;
	}

	s_state_store = state_store;
	s_events = events;
	s_wait_events = wait_events;
	s_heartbeat = heartbeat;
	return 0;
}

void soc_energy_task_entry(void *arg0, void *arg1, void *arg2)
{
	uint32_t last_sample_version = 0U;

	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	for (;;) {
		BmsSampleSnapshot sample;
		BmsFaultState fault;
		BmsEnergyState energy;

		(void)k_event_wait(s_events, s_wait_events, false, K_FOREVER);
		k_event_clear(s_events, s_wait_events);
		bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_SOC);

		if (bms_state_store_read_sample(s_state_store, &sample) != 0) {
			continue;
		}
		if (sample.version == last_sample_version) {
			continue;
		}
		last_sample_version = sample.version;

		(void)bms_state_store_read_energy(s_state_store, &energy);
		(void)bms_state_store_read_fault(s_state_store, &fault);

		bms_energy_estimator_update(&sample, &fault, &energy);

		(void)bms_state_store_write_energy(s_state_store, &energy);
	}
}
