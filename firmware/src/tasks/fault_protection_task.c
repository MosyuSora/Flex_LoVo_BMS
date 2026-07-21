#include "tasks/fault_protection_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"

/* ---- 任务自注册(见 config/app_task.h) ---- */
#if BMS_APP_ENABLE_FAULT_PROTECTION
static int fault_protection_bind_wrapper(void)
{
	return fault_protection_task_bind(app_afe, &app_state_store, &app_events,
					  BMS_EVT_SAMPLE_FAULT,
					  APP_FAULT_NOTIFY_MASK,
					  &app_protection_policy, &app_cmd_q,
					  &app_heartbeat);
}
APP_TASK_REGISTER(fault_protection, fault_protection_bind_wrapper,
		  fault_protection_task_entry, BMS_APP_FAULT_PRIORITY,
		  BMS_APP_FAULT_STACK_SIZE);
#endif

#include "config/appconfig.h"
#include "afe/bms_afe.h"
#include "algo/bms_command.h"
#include "afe/bms_afe.h"
#include "core/bms_ota_boot.h"

#include <zephyr/sys/reboot.h>

static const struct device *s_afe;
static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_wait_events;
static uint32_t s_notify_mask;
static BmsProtectionPolicy *s_policy;
static struct k_msgq *s_cmd_q;
static BmsHeartbeatTable *s_heartbeat;

int fault_protection_task_bind(const struct device *afe,
			       BmsStateStore *state_store,
			       struct k_event *events,
			       uint32_t wait_events,
			       uint32_t notify_mask,
			       BmsProtectionPolicy *policy,
			       struct k_msgq *cmd_q,
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
	s_notify_mask = notify_mask;
	s_policy = policy;
	s_cmd_q = cmd_q;
	s_heartbeat = heartbeat;
	return 0;
}

/*
 * 进 OTA 安全序列(见 OTA 设计):强关双 FET → 回读确认真关了(位翻转写不可靠,重试)
 * → 置 BKP 标志确认 → 冷复位到 Bootloader。任一步确认不过就**不复位**、留在 App 继续
 * 保护(host 会因遥测未停而知道被拒、稍后重试)。复位后 FET 由 BQ 的 SYS_CTRL2 保持关。
 */
static void fault_protection_enter_ota(void)
{
	bool off = false;

	for (int i = 0; (i < 5) && !off; ++i) {
		BmsSwitchState st;

		(void)bms_afe_set_chg_enabled(s_afe, false);
		(void)bms_afe_set_dsg_enabled(s_afe, false);
		if ((bms_afe_read_switch_state(s_afe, &st) == 0) &&
		    !st.chg_enabled && !st.dsg_enabled) {
			off = true;
		} else {
			k_sleep(K_MSEC(5));
		}
	}
	if (!off) {
		return; /* 关不掉 → 不进 OTA,继续保护 */
	}
	if (bms_ota_boot_arm()) {
		sys_reboot(SYS_REBOOT_COLD); /* 不返回 */
	}
}

static void fault_protection_drain_commands(void)
{
	BmsCommand command;

	if (s_cmd_q == NULL) {
		return;
	}

	while (k_msgq_get(s_cmd_q, &command, K_NO_WAIT) == 0) {
		if (command.type == BMS_CMD_CLEAR_FAULT) {
			bms_protection_policy_request_clear(s_policy);
		} else if (command.type == BMS_CMD_ENTER_OTA) {
			fault_protection_enter_ota();
		}
	}
}

void fault_protection_task_entry(void *arg0, void *arg1, void *arg2)
{
	BmsFaultState previous = {0};
	bool has_previous = false;

	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	for (;;) {
		BmsSampleSnapshot sample;
		BmsFaultState fault;
		uint16_t afe_status = 0U;
		bool afe_ok;
		bool changed;

		/*
		 * 等自己的采样事件位,但带超时:采样任务若因 AFE 失联
		 * 停止发布,保护任务仍要周期性醒来做失联判定和 FET 仲裁。
		 */
		(void)k_event_wait(s_events, s_wait_events, false,
				   K_MSEC(BMS_APP_SAMPLE_PERIOD_MS * 3U));
		k_event_clear(s_events, s_wait_events);
		bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_FAULT);

		fault_protection_drain_commands();

		/* 读到旧版本也继续:条件判定用旧值,计时用当前时钟 */
		if (bms_state_store_read_sample(s_state_store, &sample) != 0) {
			sample.version = 0U;
		}

		afe_ok = (bms_afe_read_fault_status(s_afe, &afe_status) == 0);
		if (afe_ok && (afe_status != 0U)) {
			/*
			 * SYS_STAT 是写 1 清除的粘滞位:读走即清,芯片才能
			 * 重新判定;策略层负责把它锁存成软件故障状态。
			 */
			(void)bms_afe_clear_fault_status(s_afe, afe_status);
		}

		bms_protection_policy_update(s_policy, &sample, afe_ok, afe_status,
					     k_uptime_get_32(), &fault);

		/*
		 * FET 控制集中在保护任务,其他任务只观察结果。
		 */
		(void)bms_afe_set_chg_enabled(s_afe, fault.charge_allowed);
		(void)bms_afe_set_dsg_enabled(s_afe, fault.discharge_allowed);

		changed = !has_previous ||
			  (fault.fault_bits != previous.fault_bits) ||
			  (fault.charge_allowed != previous.charge_allowed) ||
			  (fault.discharge_allowed != previous.discharge_allowed);
		previous = fault;
		has_previous = true;

		(void)bms_state_store_write_fault(s_state_store, &fault);
		if (changed && (s_notify_mask != 0U)) {
			k_event_post(s_events, s_notify_mask);
		}
	}
}
