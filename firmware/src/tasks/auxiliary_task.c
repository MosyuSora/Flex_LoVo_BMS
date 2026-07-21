#include "tasks/auxiliary_task.h"

#include "config/app_task.h"
#include "core/app_objects.h"

/*
 * ==========================================================================
 * 依赖注入机制——在任务文件中的体现（以 auxiliary_task 为例）
 * ==========================================================================
 *
 * 每个任务文件遵循相同的三步模板来实现依赖注入：
 *
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │ 步骤1：声明 static 变量（任务的"私有字段"）                      │
 *   │         这些变量在 bind() 中赋值，在 entry() 中使用              │
 *   │                                                                  │
 *   │ 步骤2：实现 *_task_bind() 公开函数                               │
 *   │         - 校验参数合法性（NULL 检查）                            │
 *   │         - 存入步骤1的 static 变量                                │
 *   │                                                                  │
 *   │ 步骤3：实现 *_bind_wrapper() + APP_TASK_REGISTER 宏              │
 *   │         - wrapper 桥接 app_objects.h 的全局变量到 bind()         │
 *   │         - APP_TASK_REGISTER 把任务注册到 linker 段               │
 *   └─────────────────────────────────────────────────────────────────┘
 *
 * 【为什么需要 bind_wrapper？】
 *   因为 APP_TASK_REGISTER 要求的 bind_fn 签名为 int(void)——无参数。
 *   而 *_task_bind() 需要多个参数（依赖对象）。bind_wrapper 是一个零参数的
 *   闭包，它"捕获"了 app_objects.h 中的全局变量，转调真正的 bind 函数。
 *
 *   如果 C 语言有 lambda 表达式，可以直接写成：
 *     APP_TASK_REGISTER(auxiliary,
 *         []{ return auxiliary_task_bind(&app_state_store, &app_events, ...); },
 *         auxiliary_task_entry, ...);
 *   可惜没有，所以 bind_wrapper 就是一个手写的"闭包"。
 */


/* ==========================================================================
 * 步骤1 + 步骤3：绑定包装函数 + 任务自注册
 *
 * 放在文件开头（而非末尾）是为了让阅读者一眼看到：
 *   - 这个任务依赖什么（通过 bind_wrapper 的参数列表）
 *   - 这个任务何时启动（通过 APP_TASK_REGISTER 的优先级和栈大小）
 * ========================================================================== */
#if BMS_APP_ENABLE_AUXILIARY
/*
 * bind_wrapper：将 app_objects.h 中的共享全局变量"注入"到 auxiliary_task_bind()。
 *
 * 从 main.c 的视角，它只调用 task->bind_fn()（一个无参函数），
 * 不需要知道这个任务具体需要哪些依赖。依赖知识封装在 wrapper 内部。
 *
 * 下面是 auxiliary_task 的依赖清单（即这个任务的"构造函数参数列表"）：
 *   - app_state_store    → 中心状态仓库（读故障、读电量、写自检结果）
 *   - app_events         → 事件总线（等待故障通知）
 *   - BMS_EVT_FAULT_AUX  → 本任务订阅的事件位（编译期常量）
 *   - app_heartbeat      → 心跳监控表（本任务会刷新自己的槽位）
 *   - APP_HEARTBEAT_EXPECTED_MASK → 巡检哪些槽位（编译期掩码）
 *   - app_wdt            → 看门狗设备（可能为 NULL）
 *   - app_boot_ok        → AFE 启动是否成功
 *   - BMS_APP_AUX_PERIOD_MS → 巡检周期（编译期常量）
 */
static int auxiliary_bind_wrapper(void)
{
	return auxiliary_task_bind(&app_state_store, &app_events,
				   BMS_EVT_FAULT_AUX, &app_heartbeat,
				   APP_HEARTBEAT_EXPECTED_MASK,
				   app_wdt, app_boot_ok, BMS_APP_AUX_PERIOD_MS);
}
APP_TASK_REGISTER(auxiliary, auxiliary_bind_wrapper, auxiliary_task_entry,
		  BMS_APP_AUXILIARY_PRIORITY, BMS_APP_AUXILIARY_STACK_SIZE);
#endif

#include "config/appconfig.h"
#include "indicator/bms_status_led.h"

#ifdef CONFIG_BMS_TEST_TELEMETRY
#include "core/bms_test_telemetry.h"
#endif

#if BMS_APP_ENABLE_WATCHDOG
#include <zephyr/drivers/watchdog.h>
#endif

/*
 * ==========================================================================
 * 步骤2：任务的"私有字段"——通过 bind() 注入的依赖
 * ==========================================================================
 *
 * 所有以 s_ 开头的 static 变量是本任务的"实例字段"。
 * 它们在 auxiliary_task_bind() 中赋值，在 auxiliary_task_entry() 及
 * 其调用的辅助函数中使用。
 *
 * 注意：这些是文件级 static，不是线程局部存储。每个任务只有一个实例，
 * 所以用 static 全局变量就够了——不需要 malloc，不需要"对象指针"。
 *
 * 对比面向对象语言（C++/Java）中的写法：
 *
 *   class AuxiliaryTask {
 *       BmsStateStore *state_store;    // → s_state_store
 *       struct k_event *events;        // → s_events
 *       // ...
 *   public:
 *       AuxiliaryTask(BmsStateStore *s, k_event *e, ...) {  // → auxiliary_task_bind
 *           this->state_store = s;
 *           this->events = e;
 *       }
 *       void run() { ... }             // → auxiliary_task_entry
 *   };
 *
 * 本项目用 C 语言模拟了这个模式，代价是需要手写 bind 函数的逐字段赋值。
 */
static BmsStateStore *s_state_store;
static struct k_event *s_events;
static uint32_t s_wait_events;
static BmsHeartbeatTable *s_heartbeat;
static uint32_t s_hb_expected_mask;
static const struct device *s_wdt;
static bool s_boot_ok;
static uint32_t s_period_ms;
#if BMS_APP_ENABLE_WATCHDOG
static int s_wdt_channel = -1;
#ifdef CONFIG_BMS_TEST_TELEMETRY
static bool s_wdt_setup_ok;
static uint32_t s_wdt_feed_count;
static uint32_t s_wdt_last_feed_ms;
static uint32_t s_wdt_max_gap_ms;
#endif
#endif

/*
 * ==========================================================================
 * auxiliary_task_bind()——依赖注入入口
 * ==========================================================================
 *
 * 这是本任务对外暴露的唯一"配置接口"。
 *
 * 职责：
 *   1. 参数校验：确保调用者传入了有效的依赖（NULL 检查）
 *   2. 存储依赖：将参数存入 static 变量供任务入口函数使用
 *
 * 约定：
 *   - 返回值 0 表示注入成功，非 0 表示失败（main.c 会中止启动）
 *   - 只做赋值，不做任何初始化操作（LED 和 WDT 的初始化在 entry 中做）
 *   - 这个函数在 main() 中调用，此时调度器尚未启动，没有并发问题
 *
 * 【bind 函数和构造函数有什么区别？】
 *   - bind 只负责"记住"依赖，不执行任何 I/O 或耗时操作
 *   - 真正的初始化（GPIO 配置、WDT 安装）在 auxiliary_task_entry() 中做，
 *     那时调度器已启动，可以安全地使用 Zephyr 驱动 API
 */
int auxiliary_task_bind(BmsStateStore *state_store,
			struct k_event *events,
			uint32_t wait_events,
			BmsHeartbeatTable *heartbeat,
			uint32_t heartbeat_expected_mask,
			const struct device *wdt_dev,
			bool boot_ok,
			uint32_t period_ms)
{
	if ((state_store == NULL) || (events == NULL) || (heartbeat == NULL) ||
	    (period_ms == 0U)) {
		return -1;
	}

	s_state_store = state_store;
	s_events = events;
	s_wait_events = wait_events;
	s_heartbeat = heartbeat;
	s_hb_expected_mask = heartbeat_expected_mask;
	s_wdt = wdt_dev;
	s_boot_ok = boot_ok;
	s_period_ms = period_ms;
	return 0;
}

static void auxiliary_setup_wdt(void)
{
#if BMS_APP_ENABLE_WATCHDOG
	int rc;

	if ((s_wdt == NULL) || !device_is_ready(s_wdt)) {
	#ifdef CONFIG_BMS_TEST_TELEMETRY
		bms_test_telemetry_watchdog(0U, false, 0U, 0U);
	#endif
		return;
	}

	const struct wdt_timeout_cfg cfg = {
		.window.min = 0U,
		.window.max = BMS_APP_WDT_TIMEOUT_MS,
		.flags = WDT_FLAG_RESET_SOC,
	};

	s_wdt_channel = wdt_install_timeout(s_wdt, &cfg);
	if (s_wdt_channel >= 0) {
		rc = wdt_setup(s_wdt, 0U);
	#ifdef CONFIG_BMS_TEST_TELEMETRY
		s_wdt_setup_ok = (rc == 0);
		bms_test_telemetry_watchdog(0U, s_wdt_setup_ok, 0U, 0U);
	#endif
	} else {
	#ifdef CONFIG_BMS_TEST_TELEMETRY
		bms_test_telemetry_watchdog(0U, false, 0U, 0U);
	#endif
	}
#endif
}

static void auxiliary_feed_wdt(uint32_t now_ms)
{
#if BMS_APP_ENABLE_WATCHDOG
	if ((s_wdt != NULL) && (s_wdt_channel >= 0)) {
		if (wdt_feed(s_wdt, s_wdt_channel) == 0) {
	#ifdef CONFIG_BMS_TEST_TELEMETRY
			if (s_wdt_feed_count != 0U) {
				uint32_t gap = now_ms - s_wdt_last_feed_ms;
				if (gap > s_wdt_max_gap_ms) {
					s_wdt_max_gap_ms = gap;
				}
			}
			s_wdt_last_feed_ms = now_ms;
			s_wdt_feed_count++;
			bms_test_telemetry_watchdog(now_ms, s_wdt_setup_ok,
						    s_wdt_feed_count, s_wdt_max_gap_ms);
	#endif
		}
	}
#endif
}

static void auxiliary_update_led(bool fault_latched, bool low_battery, uint32_t now_ms)
{
	/*
	 * aux 只决定"状态等级":故障或低电 -> ALERT(告警),否则 NORMAL。
	 * 怎么渲染(慢闪/快闪/呼吸)交给 bms_status_led,换 GPIO/PWM 后端本函数不动。
	 */
	BmsIndicatorState state = (fault_latched || low_battery) ? BMS_INDICATOR_ALERT
								: BMS_INDICATOR_NORMAL;
	int level = bms_status_led_render(state, now_ms);

#ifdef CONFIG_BMS_TEST_TELEMETRY
	bms_test_telemetry_led(now_ms, fault_latched, level);
#else
	ARG_UNUSED(level);
#endif
}

/*
 * ==========================================================================
 * auxiliary_task_entry()——任务主循环
 * ==========================================================================
 *
 * 此时 bind() 已经完成（由 main.c 保证），所有 s_* 变量已指向有效对象。
 * 线程启动后先做硬件初始化（LED、WDT），然后进入无限循环：
 *   等待事件 → 读取故障/电量/心跳状态 → 更新 LED → 喂狗 → 写自检结果
 */
void auxiliary_task_entry(void *arg0, void *arg1, void *arg2)
{
	ARG_UNUSED(arg0);
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);

	(void)bms_status_led_init();
	auxiliary_setup_wdt();

	for (;;) {
		BmsFaultState fault = {0};
		BmsEnergyState energy = {0};
		BmsSelftestState selftest = {0};
		bool low_battery;
		uint32_t now_ms;
		uint32_t stale;

		/* 故障变化事件让灯立刻切换节奏;平时按周期巡检 */
		(void)k_event_wait(s_events, s_wait_events, false,
				   K_MSEC(s_period_ms));
		k_event_clear(s_events, s_wait_events);

		now_ms = k_uptime_get_32();
		(void)bms_state_store_read_fault(s_state_store, &fault);

		/* 电量不足判定:能量状态可读且 SOC 低于阈值(读不到就按不缺电处理) */
		low_battery = (bms_state_store_read_energy(s_state_store, &energy) == 0) &&
			      (energy.soc_0p1pct < BMS_APP_LED_LOW_SOC_0P1PCT);

		stale = bms_heartbeat_stale_mask(s_heartbeat, now_ms,
						 BMS_APP_HEARTBEAT_STALE_MS,
						 s_hb_expected_mask);

		selftest.timestamp_ms = now_ms;
		selftest.heartbeat_stale_mask = stale;
		selftest.boot_ok = s_boot_ok;
		selftest.watchdog_enabled = (BMS_APP_ENABLE_WATCHDOG != 0);
		(void)bms_state_store_write_selftest(s_state_store, &selftest);

		auxiliary_update_led(fault.latched, low_battery, now_ms);

#ifdef CONFIG_BMS_TEST_TELEMETRY
		bms_test_telemetry_sample(now_ms);
#endif

		/* 全员心跳健康才喂狗:任何任务卡死都会让系统复位自愈 */
#if !defined(CONFIG_BMS_TEST_SKIP_WATCHDOG_FEED)
		if (stale == 0U) {
			auxiliary_feed_wdt(now_ms);
		}
#else
		ARG_UNUSED(now_ms);
#endif
	}
}
