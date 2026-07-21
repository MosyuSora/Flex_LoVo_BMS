#ifndef APP_OBJECTS_H
#define APP_OBJECTS_H

#include "config/appconfig.h"
#include "config/bms_events.h"
#include "core/bms_state_store.h"
#include "core/bms_heartbeat.h"
#include "algo/bms_protection_policy.h"
#include "algo/bms_charge_policy.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

/*
 * ==========================================================================
 * 依赖注入机制——第 2 层：共享对象声明（Composition Root 声明）
 * ==========================================================================
 *
 * 【这个文件是什么角色？】
 * 在依赖注入术语中，这里声明的所有全局变量就是"应用级的共享依赖"。
 * 它们相当于一个零件盒——main.c 在启动时先把所有零件造好（app_objects_init()），
 * 然后各个任务通过 bind 机制从这里取走自己需要的零件。
 *
 * 【依赖注入的"三板斧"在本项目的对应关系】
 *                        DI 概念                 本项目实现
 *   ─────────────────────────────────────────────────────────────────
 *   组合根（Composition Root）     app_objects.c 的 app_objects_init()
 *   服务/依赖（Services）          本文件声明的 extern 全局变量
 *   注入点（Injection Site）       各任务文件的 *_bind_wrapper() →
 *                                 *_task_bind() → static 变量
 *   ─────────────────────────────────────────────────────────────────
 *
 * 【为什么用 extern 全局变量而不是传参？】
 * 传统的"好"做法是把 main.c 里的 static 变量通过参数逐层传递，这样依赖关系
 * 清晰可见。但本项目的任务改成了自注册（APP_TASK_REGISTER），任务是在链接器
 * 段中匿名遍历的，没有一个地方能"传参"给每个任务的 bind_wrapper。
 *
 * 所以这里的 extern 是自注册架构的明确代价：依赖从 main.c 的局部变量提升为
 * 全局可见，换取"加任务不碰 main.c"。这是一个有意识的取舍，不是偷懒。
 *
 * 【所有权的约定】
 * - 本文件只声明（extern），不定义。唯一定义在 app_objects.c。
 * - 构造权集中在 app_objects_init()。各任务只读取，不修改这些对象的生命周期。
 * - 任务通过 static 变量持有指针（而非直接使用 extern），这样可以：
 *   1) 在单测中替换为 mock 对象
 *   2) 在 bind() 中做 NULL 校验
 *   3) 明确每个任务到底依赖了哪些对象
 *
 * 【设备句柄 vs 业务对象】
 * 设备句柄（app_afe、app_can、app_wdt 等）的 DI 实际上由 Zephyr 的设备驱动
 * 模型完成——DEVICE_DT_GET 在编译期解析设备树别名，拿到的是 Zephyr 初始化
 * 好的设备指针。业务对象（state_store、heartbeat、policy 等）则由本项目的
 * DI 机制管理。
 */

/* ===== AFE（模拟前端）芯片 ===== */
extern const struct device *const app_afe;

/* ===== 中心状态仓库（所有任务共享的 BMS 状态，读写需持锁） ===== */
extern BmsStateStore app_state_store;

/* ===== 事件总线（任务间异步通知，基于 Zephyr k_event） ===== */
extern struct k_event app_events;

/* ===== 心跳监控表（看门狗任务巡检各槽位是否超时未刷新） ===== */
extern BmsHeartbeatTable app_heartbeat;

/* ===== 算法策略对象（纯计算，不依赖硬件） ===== */
extern BmsProtectionPolicy app_protection_policy;
extern BmsChargePolicy app_charge_policy;

/* ===== 外部命令队列（CAN RX 中断 → 保护任务） ===== */
extern struct k_msgq app_cmd_q;

/* ===== 启动状态标志（AFE 初始化是否成功，供 AuxiliaryTask 上报） ===== */
extern bool app_boot_ok;

/* ===== 以下为条件编译的可选依赖 ===== */
#if BMS_APP_ENABLE_CAN_COMM
extern const struct device *const app_can;
#endif
#if BMS_APP_ENABLE_COMM_DIAG
extern const struct device *const app_console_uart;
#endif
#if BMS_APP_ENABLE_AUXILIARY
/* 状态灯由 bms_status_led 模块自持,不在此注入。 */
extern const struct device *const app_wdt; /* 看门狗未使能时为 NULL */
#endif

/*
 * ==========================================================================
 * 编译期事件掩码（编译期的"依赖图裁剪"）
 * ==========================================================================
 *
 * 以下三个掩码利用 BMS_APP_ENABLE_* 开关，在编译期决定哪些消费者参与事件通知、
 * 哪些任务参与心跳巡检。禁用功能对应的位直接变成 0，编译器会优化掉死代码。
 * 这相当于在编译期完成了依赖图的裁剪——不在图中的节点根本不存在于二进制中。
 */

/*
 * 采样完成事件的分发掩码。
 * 采样任务触发 k_event_post 时用这个掩码，只唤醒真正启用的消费者任务。
 * 例如：如果 CAN 通信未启用，BMS_EVT_SAMPLE_CAN 位就是 0，采样完成后
 * CAN 任务不会被唤醒。
 */
#define APP_SAMPLE_NOTIFY_MASK                                                  \
	(0U | (BMS_APP_ENABLE_FAULT_PROTECTION ? BMS_EVT_SAMPLE_FAULT : 0U) |   \
	 (BMS_APP_ENABLE_CHARGE_STATE ? BMS_EVT_SAMPLE_CHARGE : 0U) |           \
	 (BMS_APP_ENABLE_SOC_ENERGY ? BMS_EVT_SAMPLE_SOC : 0U) |                \
	 (BMS_APP_ENABLE_BALANCE ? BMS_EVT_SAMPLE_BALANCE : 0U) |               \
	 (BMS_APP_ENABLE_COMM_DIAG ? BMS_EVT_SAMPLE_DIAG : 0U) |                \
	 (BMS_APP_ENABLE_CAN_COMM ? BMS_EVT_SAMPLE_CAN : 0U))

/*
 * 故障事件分发掩码（保护任务触发故障后，通知需要响应的任务）
 */
#define APP_FAULT_NOTIFY_MASK                                                   \
	(0U | (BMS_APP_ENABLE_CAN_COMM ? BMS_EVT_FAULT_CAN : 0U) |              \
	 (BMS_APP_ENABLE_AUXILIARY ? BMS_EVT_FAULT_AUX : 0U))

/*
 * 心跳巡检范围掩码。
 * auxiliary_task 用它决定哪些槽位的心跳需要检查——未启用的任务没有心跳槽位，
 * 强行检查会误报超时。
 */
#define APP_HEARTBEAT_EXPECTED_MASK                                            \
	((1UL << BMS_HB_SLOT_SAMPLE) |                                         \
	 (BMS_APP_ENABLE_FAULT_PROTECTION ? (1UL << BMS_HB_SLOT_FAULT) : 0UL) | \
	 (BMS_APP_ENABLE_CHARGE_STATE ? (1UL << BMS_HB_SLOT_CHARGE) : 0UL) |    \
	 (BMS_APP_ENABLE_BALANCE ? (1UL << BMS_HB_SLOT_BALANCE) : 0UL) |        \
	 (BMS_APP_ENABLE_SOC_ENERGY ? (1UL << BMS_HB_SLOT_SOC) : 0UL) |         \
	 (BMS_APP_ENABLE_CAN_COMM ? (1UL << BMS_HB_SLOT_CAN) : 0UL) |           \
	 (BMS_APP_ENABLE_COMM_DIAG ? (1UL << BMS_HB_SLOT_DIAG) : 0UL))

/*
 * 组合根初始化函数。
 * 在 main() 最开头调用一次，在所有任务 bind/start 之前完成。
 * 注意：这里只"构造"，不做"接线"——接线由各任务的 bind_fn 完成。
 */
int app_objects_init(void);

#endif /* APP_OBJECTS_H */
