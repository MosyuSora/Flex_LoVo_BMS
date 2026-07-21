/*
 * ==========================================================================
 * 依赖注入机制——第 0 层：Linker 可迭代段（Iterable Section）
 * ==========================================================================
 *
 * 【这是什么】
 * 本文件在链接脚本中登记一个名为 `app_task_def` 的自定义段（section）。
 * 每个任务文件中的 APP_TASK_REGISTER 宏会把任务元信息（入口函数、优先级、
 * 栈大小等）写入这个段，链接器最终将所有散落在各 .o 文件中的同段数据合并
 * 成一个连续数组，并提供 _app_task_def_list_start / _app_task_def_list_end
 * 两个符号标记数组首尾。main.c 用 STRUCT_SECTION_FOREACH 遍历这个数组，
 * 逐个调用 bind_fn() 注入依赖，然后创建线程。
 *
 * 【为什么需要这个——嵌入式工程师视角】
 * 传统做法是 main.c 里手动罗列所有任务初始化代码：
 *
 *     sample_task_init(&app_afe, &app_state_store, ...);
 *     k_thread_create(&sample_thread, ...);
 *     fault_task_init(...);
 *     k_thread_create(&fault_thread, ...);
 *     // ... 每加一个任务都要在这里加两行
 *
 * 这导致 main.c 随着任务增多不断膨胀，且条件编译（哪些任务启用）的逻辑
 * 散落在两处：任务文件本身 + main.c 的任务列表。利用链接器段的技巧，每个
 * 任务"自注册"到段里，main.c 只需一个循环遍历——加任务 = 写任务文件 +
 * 加入 CMake 源表，main.c 一行不动。这就是编译期的"控制反转"：main.c
 * 不再主动知道有哪些任务，而是被任务告知。
 *
 * 【类比——如果你用过 Zephyr 的设备驱动模型】
 * 这和 DEVICE_DEFINE / SYS_INIT 完全是一回事：驱动文件调用宏把自己注册
 * 进某个 linker 段，系统启动时遍历段内的所有条目依次初始化。本项目的
 * app_task_def 段就是在应用层复刻了这个机制。
 *
 * 【RAM vs ROM】
 * 任务描述结构体放在 RAM（ITERABLE_SECTION_RAM），因为里面有指针字段
 * （bind_fn、entry、stack、thread）——函数指针和线程对象地址在链接时才
 * 确定，不能放 ROM。
 */
ITERABLE_SECTION_RAM(app_task_def, Z_LINK_ITERABLE_SUBALIGN)
