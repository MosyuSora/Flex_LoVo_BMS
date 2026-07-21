#ifndef APP_TASK_H
#define APP_TASK_H

#include <zephyr/kernel.h>
#include <zephyr/sys/iterable_sections.h>

/*
 * ==========================================================================
 * 依赖注入机制——第 1 层：任务自注册（Self-Registration）
 * ==========================================================================
 *
 * 【核心概念】
 * 每个任务文件通过 APP_TASK_REGISTER 宏把自身"注册"到链接器的 app_task_def
 * 段中（见 app_task_iterable.ld）。main.c 遍历这个段，对每个条目依次执行：
 *     task->bind_fn()           →  注入依赖（把共享对象传入任务的 static 变量）
 *     k_thread_create(...)      →  创建线程
 *
 * 这就是依赖注入（DI）的嵌入式实现——没有框架、没有反射、没有动态内存分配，
 * 一切在编译期就确定好了。
 *
 * 【和传统做法的对比】
 * ─────────────────────────────────────────────────────────────────────
 *  传统做法（到处 #ifdef）         本项目做法（自注册 DI）
 * ─────────────────────────────────────────────────────────────────────
 *  main.c 中：                    main.c 中：
 *  #if ENABLE_FAULT               STRUCT_SECTION_FOREACH(app_task_def, t) {
 *    fault_init(...);                 t->bind_fn();
 *    k_thread_create(...);            k_thread_create(...);
 *  #endif                         }
 *  #if ENABLE_CHARGE
 *    charge_init(...);            而 fault_task.c 中：
 *    ...                          APP_TASK_REGISTER(fault, fault_bind_wrapper,
 *  #endif                                            fault_task_entry,
 *  ...重复 N 次...                                   prio, stack_sz);
 * ─────────────────────────────────────────────────────────────────────
 *  问题：                          好处：
 *  - main.c 随任务数量膨胀         - main.c 永远只有一段循环
 *  - 条件编译散落两处              - 条件编译只在任务文件自身
 *  - 加任务必须改 main.c           - 加任务 = 加文件，main.c 不动
 * ─────────────────────────────────────────────────────────────────────
 *
 *
 * 【app_task_def 结构体——任务元信息】
 * 每个任务在段中存放一条这样的记录。注意 bind_fn 不是任务的业务函数，
 * 而是依赖注入的"接线函数"——它负责把共享对象指针存到任务文件的
 * static 变量中，使得任务入口函数后续可以访问。
 */
struct app_task_def {
	/*
	 * 依赖绑定函数。由各任务文件的 *_bind_wrapper() 提供。
	 * 在 k_thread_create 之前调用。返回非 0 表示依赖校验失败
	 * （例如某个必需的设备句柄为 NULL），main.c 会中止启动。
	 *
	 * 这是依赖注入的"注入"步骤：把 app_objects.h 中声明的共享
	 * 全局对象（如 app_afe、app_state_store 等）传递给任务的
	 * static 变量。
	 */
	int (*bind_fn)(void);

	/* 任务线程入口函数，例如 auxiliary_task_entry */
	k_thread_entry_t entry;

	/* Zephyr 线程优先级，数值越小优先级越高 */
	int priority;

	/* 线程栈（由 APP_TASK_REGISTER 宏自动创建 K_THREAD_STACK_DEFINE） */
	k_thread_stack_t *stack;

	/* 栈大小（字节），由 K_THREAD_STACK_SIZEOF 在编译期计算 */
	size_t stack_size;

	/* 线程控制块（由 APP_TASK_REGISTER 宏自动声明） */
	struct k_thread *thread;

	/* 线程名，用于调试（由宏自动设为任务 id 的字符串形式） */
	const char *name;
};

/*
 * APP_TASK_REGISTER 宏——每个任务文件调用一次
 *
 * 参数说明：
 *   id        : 任务标识符（C 标识符，不用引号）。宏内部用它拼接变量名：
 *               比如 id=sample → sample_stack、sample_thread、sample_task_def
 *   bind_fn_  : 绑定包装函数，类型 int(void)。在这个函数里调用真正的 *_task_bind()
 *   entry_    : 线程入口函数
 *   prio_     : 线程优先级
 *   stack_sz_ : 栈大小（字节）
 *
 * 使用示例（见 auxiliary_task.c）：
 *
 *   static int auxiliary_bind_wrapper(void)
 *   {
 *       return auxiliary_task_bind(&app_state_store, &app_events,
 *                                   BMS_EVT_FAULT_AUX, &app_heartbeat,
 *                                   APP_HEARTBEAT_EXPECTED_MASK,
 *                                   app_wdt, app_boot_ok, BMS_APP_AUX_PERIOD_MS);
 *   }
 *   APP_TASK_REGISTER(auxiliary, auxiliary_bind_wrapper, auxiliary_task_entry,
 *                     BMS_APP_AUXILIARY_PRIORITY, BMS_APP_AUXILIARY_STACK_SIZE);
 *
 * 宏展开后实际生成：
 *   - K_THREAD_STACK_DEFINE(auxiliary_stack, stack_sz_)  → 分配栈空间
 *   - static struct k_thread auxiliary_thread            → 分配线程控制块
 *   - 一个 STRUCT_SECTION_ITERABLE 条目                  → 注册到 linker 段
 */
#define APP_TASK_REGISTER(id, bind_fn_, entry_, prio_, stack_sz_)               \
	/* 1. 分配线程栈空间 */                                                 \
	static K_THREAD_STACK_DEFINE(id##_stack, stack_sz_);                    \
	/* 2. 声明线程控制块 */                                                 \
	static struct k_thread id##_thread;                                     \
	/* 3. 将任务元信息放入 linker 的 app_task_def 段 */                     \
	static STRUCT_SECTION_ITERABLE(app_task_def, id##_task_def) = {         \
		.bind_fn = (bind_fn_),                                          \
		.entry = (entry_),                                             \
		.priority = (prio_),                                           \
		.stack = id##_stack,                                          \
		.stack_size = K_THREAD_STACK_SIZEOF(id##_stack),             \
		.thread = &id##_thread,                                       \
		.name = #id,                                                  \
	}

#endif /* APP_TASK_H */
