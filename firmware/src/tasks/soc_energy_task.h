#ifndef SOC_ENERGY_TASK_H
#define SOC_ENERGY_TASK_H

#include "core/bms_heartbeat.h"
#include "core/bms_state_store.h"

#include <stdint.h>
#include <zephyr/kernel.h>

/* SocEnergyTask 只做同步和状态搬运,算法在 bms_energy_estimator */
int soc_energy_task_bind(BmsStateStore *state_store,
			 struct k_event *events,
			 uint32_t wait_events,
			 BmsHeartbeatTable *heartbeat);
void soc_energy_task_entry(void *arg0, void *arg1, void *arg2);

#endif
