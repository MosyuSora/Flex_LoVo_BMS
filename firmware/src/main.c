#include "config/app_task.h"
#include "core/app_objects.h"

#include <zephyr/kernel.h>

/*
 * ==========================================================================
 * 依赖注入机制——第 3 层：装配入口（Assembly Entry）
 * ==========================================================================
 *
 * 【main.c 的角色——"装配根"（Composition Root）】
 *
 * 在依赖注入架构中，main() 是唯一一处"知道全局"的地方。它负责两个步骤：
 *
 *   步骤1：构造（Construct）    步骤2：接线 + 启动（Wire & Start）
 *   ──────────────────────      ─────────────────────────────────
 *   app_objects_init()           STRUCT_SECTION_FOREACH(app_task_def, t) {
 *     ├─ k_event_init              t->bind_fn()    ← 注入依赖
 *     ├─ bms_heartbeat_init        k_thread_create ← 启动线程
 *     └─ ...                     }
 *
 *   问题：对象从无到有              问题：谁需要什么对象？
 *   答案：只有这里有答案            答案：每个任务自己知道（自注册 + bind_wrapper）
 *
 * 这就是"依赖注入"四个字的含义——main() 不再硬编码"sample_task 需要 app_afe
 * 和 app_state_store"这种知识，而是让每个任务自己声明需要什么。main() 只管
 * 遍历 + 绑定 + 启动。
 *
 *
 * 【和传统单片机固件 main() 的对比】
 *
 *   传统写法：
 *     int main(void) {
 *         hal_init();
 *         #if ENABLE_FEATURE_A
 *         task_a_init(&global_x, &global_y);
 *         k_thread_create(&task_a_thread, ...);
 *         #endif
 *         #if ENABLE_FEATURE_B
 *         task_b_init_only_global_z();
 *         k_thread_create(&task_b_thread, ...);
 *         #endif
 *         // 每加一个功能就要加一段 #if ... #endif
 *     }
 *
 *   问题：main() 随功能增多线性膨胀，条件编译散落各处。
 *
 *   本项目写法：
 *     int main(void) {
 *         app_objects_init();                   // 构造
 *         STRUCT_SECTION_FOREACH(app_task_def, task) {
 *             task->bind_fn();                  // 注入依赖
 *             k_thread_create(...);             // 启动线程
 *         }
 *     }
 *
 *   这个循环永远不变。加功能 = 加任务文件 + APP_TASK_REGISTER，
 *   编译器自动把新条目链接到 app_task_def 段中。
 *
 *
 * 【启动失败策略】
 *   - app_objects_init() 失败 → 业务对象构造失败（如互斥锁故障），立即 return
 *   - bind_fn() 失败 → 某个任务的依赖校验不通过（如必需的设备不存在），立即 return
 *
 *   两者都会导致 main() 返回 -1，系统不会启动任何任务线程。在嵌入式场景中
 *   这意味着系统停摆——可以考虑在这里加一个死循环 + 看门狗复位，但当前版本
 *   选择"快速失败"，让问题在开发阶段暴露。
 */
int main(void)
{
	/*
	 * 步骤1：构造所有共享对象。
	 * 这里只造零件，不管谁用。零件清单见 app_objects.c。
	 */
	if (app_objects_init() != 0) {
		/* 基础业务对象构造失败（如互斥锁），不可恢复 */
		return -1;
	}

	/*
	 * 步骤2：遍历链接器收集的任务表，逐一接线并启动。
	 *
	 * STRUCT_SECTION_FOREACH 利用链接器提供的 _start / _end 符号，
	 * 遍历 app_task_def 段中的所有条目。每个条目来自不同 .c 文件中的
	 * APP_TASK_REGISTER 宏——main.c 不关心有几个任务、是什么任务。
	 *
	 * bind_fn() 内部调用 *_bind_wrapper()，本质是：
	 *     some_task_bind(&app_afe, &app_state_store, &app_events, ...)
	 * 把共享全局对象"注入"到任务文件的 static 变量中。
	 *
	 * bind 成功后才启动线程。如果某个任务的依赖校验失败，系统不会
	 * 半启动（前面的任务也不会回滚——嵌入式固件不做事务性启动）。
	 */
	STRUCT_SECTION_FOREACH(app_task_def, task) {
		if ((task->bind_fn != NULL) && (task->bind_fn() != 0)) {
			/* 依赖注入失败：某个任务的必需依赖不满足 */
			return -1;
		}
		k_thread_create(task->thread, task->stack, task->stack_size,
				task->entry, NULL, NULL, NULL,
				task->priority, 0, K_NO_WAIT);
		k_thread_name_set(task->thread, task->name);
	}

	/*
	 * main 退场，剩下的工作由各业务线程接管。
	 * Zephyr 的 main 线程退出后不会回收——它只是不再参与调度。
	 */
	return 0;
}
