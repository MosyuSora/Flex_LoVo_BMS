#include "core/app_objects.h"

#include "afe/bms_afe.h"
#include "algo/bms_command.h"

/*
 * ==========================================================================
 * 依赖注入机制——第 2 层（续）：共享对象的唯一定义 + 构造
 * ==========================================================================
 *
 * 【这个文件做什么】
 * 本文件是 app_objects.h 中所有 extern 声明的唯一定义处。它做两件事：
 *
 *   1. 定义（分配存储）所有共享对象
 *      - 设备句柄通过 Zephyr 的 DEVICE_DT_GET 在编译期求解
 *      - 业务对象在此处分配内存
 *
 *   2. app_objects_init() 中初始化/构造这些对象
 *      - 调用 k_event_init、bms_state_store_construct 等初始化函数
 *      - 唤醒 AFE 芯片（bms_afe_init）
 *
 * 【和依赖注入的关系——"构造"和"接线"的分离】
 *
 *   构造（本文件）                  接线（各任务 bind_wrapper）
 *   ─────────────                   ──────────────────────────
 *   k_event_init(&app_events)       auxiliary_task_bind(... &app_events ...)
 *   bms_heartbeat_init(...)         sample_task_bind(... &app_heartbeat ...)
 *   bms_protection_policy_init(...) fault_task_bind(... &app_protection_policy ...)
 *        ↑                                    ↑
 *     把零件造出来                          把零件"塞给"任务
 *     (app_objects_init)                    (各 *_bind_wrapper)
 *
 *   这种分离的好处是：如果你写单元测试，可以跳过 app_objects_init()，
 *   直接手写 mock 对象，然后调用 task_bind() 传入 mock——任务的业务逻辑
 *   不需要任何修改。
 *
 * 【"构造"和"接线"都在 main() 中调用，但它们是两个步骤】
 *   main.c:
 *     app_objects_init();             ← 步骤1：构造（本文件）
 *     STRUCT_SECTION_FOREACH(...) {   ← 步骤2：接线 + 启动（各任务文件）
 *         task->bind_fn();            ←    这里调的是 bind_wrapper
 *         k_thread_create(...);
 *     }
 */


/*
 * ==========================================================================
 * 设备句柄——通过 Zephyr 设备树解析
 * ==========================================================================
 *
 * Zephyr 的设备驱动模型本身就是一种 DI：
 *   - 驱动文件用 DEVICE_DT_INST_DEFINE 注册设备（提供方）
 *   - 应用层用 DEVICE_DT_GET 获取设备指针（消费方）
 *   - 设备树别名（DT_ALIAS）相当于"接口名"，具体绑定哪个芯片由 overlay 决定
 *
 * 例如这里的 app_afe：改芯片型号只需在 .overlay 文件中改 bms_afe 别名指向，
 * 代码一行不动。AFE 驱动本身还有一个 vtable 接口层（见 afe/bms_afe.h），
 * 不同芯片实现同一套 vtable，这是硬件层的依赖倒置。
 */
#define BMS_AFE_NODE DT_ALIAS(bms_afe)
BUILD_ASSERT(DT_NODE_HAS_STATUS(BMS_AFE_NODE, okay), "Missing bms-afe alias");

/* ---- 必选依赖（所有变体都需要） ---- */
const struct device *const app_afe = DEVICE_DT_GET(BMS_AFE_NODE);
BmsStateStore app_state_store;
struct k_event app_events;
BmsHeartbeatTable app_heartbeat;
BmsProtectionPolicy app_protection_policy;
BmsChargePolicy app_charge_policy;
K_MSGQ_DEFINE(app_cmd_q, sizeof(BmsCommand), 4, 4);
bool app_boot_ok;

/* ---- 可选依赖（根据 appconfig.h 的开关条件编译） ---- */
#if BMS_APP_ENABLE_CAN_COMM
const struct device *const app_can = DEVICE_DT_GET(DT_ALIAS(bms_can));
#endif
#if BMS_APP_ENABLE_COMM_DIAG
const struct device *const app_console_uart =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif
#if BMS_APP_ENABLE_AUXILIARY
/* 状态灯设备由 bms_status_led 模块自持(drivers 层直接用 DT),此处不再定义。 */
#if BMS_APP_ENABLE_WATCHDOG
const struct device *const app_wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
#else
const struct device *const app_wdt = NULL;
#endif
#endif

/*
 * ==========================================================================
 * app_objects_init()——组合根初始化
 * ==========================================================================
 *
 * 调用时机：main() 最开头，在所有任务线程创建之前。
 * 调用顺序：先构造基础设施（事件、心跳），再构造数据层（状态仓库），
 *           最后初始化硬件（AFE）和算法对象。
 *
 * 返回值：0 = 成功；非 0 = 基础对象构造失败（此时不应继续启动）。
 *
 * 注意：AFE 初始化失败不视为致命错误——app_boot_ok 记录结果，
 * auxiliary_task 会上报，保护任务的失联检测也会接管。这与"业务对象构造失败"
 * （例如互斥锁初始化失败）不同：AFE 是外部硬件，可能暂时不响应；
 * 业务对象是本机资源，失败意味着系统状态不可信。
 */
int app_objects_init(void)
{
	k_event_init(&app_events);
	bms_heartbeat_init(&app_heartbeat);

	if (bms_state_store_construct(&app_state_store) != 0) {
		return -1;
	}

	/*
	 * AFE 初始化失败不终止启动：
	 * 记入 boot_ok，让保护任务的失联检测接管（FET 保持关断），
	 * 而不是整个系统装配不出来。auxiliary_task 会上报此状态。
	 */
	app_boot_ok = (bms_afe_init(app_afe) == 0);

	/* 算法对象：纯 C 数据结构的初始化，不依赖硬件 */
	bms_protection_policy_init(&app_protection_policy);
	bms_charge_policy_init(&app_charge_policy);

	return 0;
}
