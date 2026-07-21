#include "tasks/can_comm_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"

/* ---- 任务自注册(见 config/app_task.h) ---- */
#if BMS_APP_ENABLE_CAN_COMM
static int can_comm_bind_wrapper(void)
{
	return can_comm_task_bind(app_can, &app_state_store, &app_events,
				  BMS_EVT_SAMPLE_CAN | BMS_EVT_FAULT_CAN,
				  &app_cmd_q, BMS_APP_CAN_TX_PERIOD_MS,
				  &app_heartbeat);
}
APP_TASK_REGISTER(can_comm, can_comm_bind_wrapper, can_comm_task_entry,
		  BMS_APP_CAN_COMM_PRIORITY, BMS_APP_CAN_COMM_STACK_SIZE);
#endif

#include "algo/bms_protocol_codec.h"

#ifdef CONFIG_BMS_TEST_TELEMETRY
#include "core/bms_test_telemetry.h"
#endif

#include <string.h>
#include <zephyr/drivers/can.h>

static const struct device *s_can;
static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_wait_events;
static struct k_msgq *s_cmd_q;
static uint32_t s_tx_period_ms;
static BmsHeartbeatTable *s_heartbeat;

/* RX 回调只投递,解码在任务上下文做(回调运行在 ISR/驱动上下文) */
K_MSGQ_DEFINE(s_can_rx_q, sizeof(struct can_frame), 4, 4);

static void can_comm_rx_callback(const struct device *dev,
				 struct can_frame *frame,
				 void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	(void)k_msgq_put(&s_can_rx_q, frame, K_NO_WAIT);
}

int can_comm_task_bind(const struct device *can_dev,
		       BmsStateStore *state_store,
		       struct k_event *events,
		       uint32_t wait_events,
		       struct k_msgq *cmd_q,
		       uint32_t tx_period_ms,
		       BmsHeartbeatTable *heartbeat)
{
	if ((can_dev == NULL) || (state_store == NULL) || (events == NULL) ||
	    (wait_events == 0U) || (tx_period_ms == 0U)) {
		return -1;
	}

	s_can = can_dev;
	s_state_store = state_store;
	s_events = events;
	s_wait_events = wait_events;
	s_cmd_q = cmd_q;
	s_tx_period_ms = tx_period_ms;
	s_heartbeat = heartbeat;
	return 0;
}

static int can_comm_setup(void)
{
	const struct can_filter cmd_filter = {
		.flags = CAN_FILTER_IDE,
		.id = BMS_PROTOCOL_CMD_FRAME_ID,
		.mask = CAN_EXT_ID_MASK,
	};
	int rc;

	if (!device_is_ready(s_can)) {
	#ifdef CONFIG_BMS_TEST_TELEMETRY
		bms_test_telemetry_can_state(k_uptime_get_32(), -ENODEV, -1);
	#endif
		return -1;
	}

	rc = can_start(s_can);
	if ((rc != 0) && (rc != -EALREADY)) {
	#ifdef CONFIG_BMS_TEST_TELEMETRY
		bms_test_telemetry_can_state(k_uptime_get_32(), rc, -1);
	#endif
		return rc;
	}

	rc = can_add_rx_filter(s_can, can_comm_rx_callback, NULL, &cmd_filter);
	#ifdef CONFIG_BMS_TEST_TELEMETRY
	bms_test_telemetry_can_state(k_uptime_get_32(), rc, rc);
	#endif
	return (rc < 0) ? rc : 0;
}

static void can_comm_process_rx(void)
{
	struct can_frame frame;

	while (k_msgq_get(&s_can_rx_q, &frame, K_NO_WAIT) == 0) {
		BmsProtocolFrame decoded;
		BmsCommand command;

		decoded.id = frame.id;
		decoded.dlc = MIN(can_dlc_to_bytes(frame.dlc),
				  BMS_PROTOCOL_FRAME_DLC);
		memcpy(decoded.data, frame.data, decoded.dlc);

		if ((bms_protocol_decode_command(&decoded, &command) == 0) &&
		    (s_cmd_q != NULL)) {
			(void)k_msgq_put(s_cmd_q, &command, K_NO_WAIT);
		}
	}
}

/*
 * bxCAN 无 DMA 通路——发送靠 3 个硬件发送邮箱。传非 NULL 回调即异步:can_send
 * 把帧塞进空邮箱就返回,硬件发完再回调,任务不再同步阻塞等发完(对比原来的
 * NULL 回调是阻塞等发送完成)。这是 F103 上"CAN 版的 DMA":把搬运交给硬件。
 */
static void can_comm_tx_callback(const struct device *dev, int error, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	ARG_UNUSED(error);
}

static void can_comm_broadcast(void)
{
	BmsSampleSnapshot sample;
	BmsEnergyState energy;
	BmsPowerState power;

	if (bms_state_store_read_sample(s_state_store, &sample) != 0) {
		return;
	}
	if (sample.version == 0U) {
		return; /* 还没有真实采样,不广播全 0 数据 */
	}
	(void)bms_state_store_read_energy(s_state_store, &energy);
	(void)bms_state_store_read_power(s_state_store, &power);

	for (uint8_t no = 1U; no <= BMS_PROTOCOL_FRAME_COUNT; ++no) {
		BmsProtocolFrame encoded;
		struct can_frame frame;

		if (bms_protocol_encode_frame(no, &sample, &energy, &power,
					      &encoded) != 0) {
			continue;
		}

		memset(&frame, 0, sizeof(frame));
		frame.id = encoded.id;
		frame.flags = CAN_FRAME_IDE;
		frame.dlc = can_bytes_to_dlc(encoded.dlc);
		memcpy(frame.data, encoded.data, encoded.dlc);

		/*
		 * 异步入队:回调非 NULL,can_send 塞进空邮箱即返回;K_MSEC(2) 只用于
		 * 等一个空邮箱(3 个邮箱满时短暂等待),不等发送完成。总线异常时丢帧,
		 * 绝不阻塞任务节拍。
		 */
		int rc = can_send(s_can, &frame, K_MSEC(2), can_comm_tx_callback, NULL);
#ifdef CONFIG_BMS_TEST_TELEMETRY
		bms_test_telemetry_can_tx(k_uptime_get_32(), frame.id,
					  encoded.dlc, frame.data, rc);
#else
		ARG_UNUSED(rc);
#endif
	}
}

void can_comm_task_entry(void *arg0, void *arg1, void *arg2)
{
	int64_t next_tx;

	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	if (can_comm_setup() != 0) {
		/*
		 * CAN 控制器不可用(仿真平台模型缺失或真机总线故障):
		 * 任务自我降级为心跳保活,不拖垮其余业务。
		 */
		for (;;) {
			bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_CAN);
			k_sleep(K_MSEC(s_tx_period_ms * 10U));
		}
	}

	next_tx = k_uptime_get();
	for (;;) {
		(void)k_event_wait(s_events, s_wait_events, false,
				   K_MSEC(s_tx_period_ms));
		k_event_clear(s_events, s_wait_events);
		bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_CAN);

		can_comm_process_rx();

		if (k_uptime_get() >= next_tx) {
			can_comm_broadcast();
			next_tx += s_tx_period_ms;
		}
	}
}
