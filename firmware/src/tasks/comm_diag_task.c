#include "tasks/comm_diag_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"
#include "afe/bms_afe.h"

/* ---- 任务自注册(见 config/app_task.h) ---- */
#if BMS_APP_ENABLE_COMM_DIAG
static int comm_diag_bind_wrapper(void)
{
	return comm_diag_task_bind(app_console_uart, &app_state_store,
				   &app_events, BMS_EVT_SAMPLE_DIAG,
				   BMS_APP_COMM_EVENT_WAIT_TIMEOUT_MS,
				   &app_heartbeat);
}
APP_TASK_REGISTER(comm_diag, comm_diag_bind_wrapper, comm_diag_task_entry,
		  BMS_APP_COMM_DIAG_PRIORITY, BMS_APP_COMM_DIAG_STACK_SIZE);
#endif

#include "algo/bms_diag_format.h"

#include <stddef.h>
#include <string.h>
#include <zephyr/drivers/uart.h>

/*
 * 诊断口直接用 Zephyr UART 驱动(uart_poll_out/in)——UART 驱动 Zephyr 已提供,
 * 不再套自造的通信父类。s_uart 由装配根注入(zephyr,console 选定的那个)。
 */
static const struct device *s_uart;
static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_wait_events;
static uint32_t s_event_wait_timeout_ms;
static BmsHeartbeatTable *s_heartbeat;
static uint32_t s_last_sample_version;

#if defined(CONFIG_UART_ASYNC_API)
/*
 * DMA 异步发送(真机):uart_tx 把整块交给 DMA,comm_diag 线程睡在信号量上,
 * 不再逐字节 uart_poll_out 忙等(115200 下一条 SNAP 原本要占 ~22ms CPU)。
 * 缓冲在 DMA 搬完前必须有效——line[] 是 static、banner/错误串是字面量,均满足。
 */
static K_SEM_DEFINE(s_tx_done, 0, 1);

static void comm_diag_uart_callback(const struct device *dev,
				    struct uart_event *evt, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	if ((evt->type == UART_TX_DONE) || (evt->type == UART_TX_ABORTED)) {
		k_sem_give(&s_tx_done);
	}
}

static int comm_diag_send(const uint8_t *data, size_t length)
{
	if ((s_uart == NULL) || (data == NULL) || !device_is_ready(s_uart)) {
		return -1;
	}
	if (uart_tx(s_uart, data, length, 100000) != 0) {
		return -1;
	}
	/* DMA 搬运期间让出 CPU;发完或中止由回调给信号量(200ms 兜底防丢事件) */
	(void)k_sem_take(&s_tx_done, K_MSEC(200));
	return 0;
}
#else
/* 无 DMA 平台(Renode 仿真不建模 DMA):退回逐字节 poll_out 忙等。 */
static int comm_diag_send(const uint8_t *data, size_t length)
{
	if ((s_uart == NULL) || (data == NULL) || !device_is_ready(s_uart)) {
		return -1;
	}
	for (size_t i = 0U; i < length; ++i) {
		uart_poll_out(s_uart, data[i]);
	}
	return 0;
}
#endif

static int comm_diag_write_line(const char *text)
{
	return comm_diag_send((const uint8_t *)text, strlen(text));
}

static int comm_diag_write_snapshot(void)
{
	BmsSampleSnapshot snap;
	BmsEnergyState energy;
	BmsFaultState fault;
	BmsBalanceState balance;
	BmsPowerState power;
	BmsSelftestState selftest;
	BmsSnapshotView view = {0};
	/*
	 * 384 B 输出缓冲移出栈:诊断线程栈只有 768 B,这块放栈上会吃掉一半。
	 * comm_diag 单线程调用,static 无重入风险,搬到 .bss 后栈头留出 ~400 B。
	 */
	static char line[384];
	size_t n;

	/* 逐仓库读:读到才把指针挂进 view,读失败的段格式化器自动跳过。 */
	if (bms_state_store_read_sample(s_state_store, &snap) != 0) {
		return comm_diag_write_line("ERR snapshot\r\n");
	}
	view.sample = &snap;
	if (bms_state_store_read_energy(s_state_store, &energy) == 0) {
		view.energy = &energy;
	}
	if (bms_state_store_read_fault(s_state_store, &fault) == 0) {
		view.fault = &fault;
	}
	if (bms_state_store_read_balance(s_state_store, &balance) == 0) {
		view.balance = &balance;
	}
	if (bms_state_store_read_power(s_state_store, &power) == 0) {
		view.power = &power;
	}
	if (bms_state_store_read_selftest(s_state_store, &selftest) == 0) {
		view.selftest = &selftest;
	}

	/* 纯格式化交给 bms_diag_format;本任务只管读仓库 + 发送。 */
	n = bms_diag_format_snapshot(line, sizeof(line), &view);
	return comm_diag_send((const uint8_t *)line, n);
}

static void comm_diag_write_afe_registers(void)
{
	/* 0x00..0x59 覆盖 BQ76940 已定义寄存器和中间保留地址，便于对照芯片手册。 */
	static char line[64];

	for (uint16_t reg = 0U; reg <= 0x59U; ++reg) {
		uint8_t value = 0U;
		int rc = bms_afe_read_register(app_afe, (uint8_t)reg, &value);
		size_t n = bms_diag_format_afe_reg(line, sizeof(line), (uint8_t)reg,
						   rc, value);

		(void)comm_diag_send((const uint8_t *)line, n);
	}
	(void)comm_diag_write_line("AFE_REG_END\r\n");
}

static void comm_diag_handle_command(uint8_t command)
{
	if ((command == '\r') || (command == '\n') || (command == ' ')) {
		return;
	}
	if ((command == 'S') || (command == 's') || (command == '?')) {
		(void)comm_diag_write_snapshot();
		return;
	}
	if ((command == 'R') || (command == 'r')) {
		comm_diag_write_afe_registers();
		return;
	}

	(void)comm_diag_write_line("ERR command\r\n");
}

static void comm_diag_poll_command(void)
{
	unsigned char command;

	if ((s_uart == NULL) || !device_is_ready(s_uart)) {
		return;
	}
	/* uart_poll_in:有字节返回 0,无字节返回 -1(非阻塞) */
	if (uart_poll_in(s_uart, &command) == 0) {
		comm_diag_handle_command((uint8_t)command);
	}
}

int comm_diag_task_bind(const struct device *uart,
			BmsStateStore *state_store,
			struct k_event *events,
			uint32_t wait_events,
			uint32_t event_wait_timeout_ms,
			BmsHeartbeatTable *heartbeat)
{
	if ((uart == NULL) || (state_store == NULL) || (events == NULL) ||
	    (wait_events == 0U) || (event_wait_timeout_ms == 0U)) {
		return -1;
	}

	s_uart = uart;
	s_state_store = state_store;
	s_events = events;
	s_wait_events = wait_events;
	s_event_wait_timeout_ms = event_wait_timeout_ms;
	s_heartbeat = heartbeat;
	return 0;
}

void comm_diag_task_entry(void *arg0, void *arg1, void *arg2)
{
	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

#if defined(CONFIG_UART_ASYNC_API)
	if ((s_uart != NULL) && device_is_ready(s_uart)) {
		(void)uart_callback_set(s_uart, comm_diag_uart_callback, NULL);
	}
#endif

	(void)comm_diag_write_line("BMS UART diag ready. Send S for snapshot.\r\n");

	for (;;) {
		uint32_t events;

		/* 带超时等自己的位:没有新采样时也要轮询 RX 命令 */
		events = k_event_wait(s_events, s_wait_events, false,
				      K_MSEC(s_event_wait_timeout_ms));
		if ((events & s_wait_events) != 0U) {
			BmsSampleSnapshot snap;

			k_event_clear(s_events, s_wait_events);
			if ((bms_state_store_read_sample(s_state_store, &snap) == 0) &&
			    (snap.version != s_last_sample_version)) {
				s_last_sample_version = snap.version;
				(void)comm_diag_write_snapshot();
			}
		}
		bms_heartbeat_beat(s_heartbeat, BMS_HB_SLOT_DIAG);
		comm_diag_poll_command();
	}
}
