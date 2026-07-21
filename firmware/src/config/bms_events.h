#ifndef BMS_EVENTS_H
#define BMS_EVENTS_H

#include <zephyr/sys/util_macro.h>

/*
 * 事件位分配:每个消费者独占一个位。
 *
 * 设计规则(修复旧版"共享位无人清除退化成轮询"的教训):
 * - 生产者 post 一个由装配根(main.c)组装的 notify_mask(只包含已启用的消费者)。
 * - 每个消费者只等自己的位,处理前 k_event_clear() 只清自己的位。
 * - 谁生产、谁消费的装配知识留在装配根,任务源码不感知同伴。
 */

/* sample-ready 组:SampleTask 广播 */
#define BMS_EVT_SAMPLE_FAULT BIT(0)
#define BMS_EVT_SAMPLE_CHARGE BIT(1)
#define BMS_EVT_SAMPLE_SOC BIT(2)
#define BMS_EVT_SAMPLE_BALANCE BIT(3)
#define BMS_EVT_SAMPLE_DIAG BIT(4)
#define BMS_EVT_SAMPLE_CAN BIT(5)

/* fault-changed 组:FaultProtectionTask 在故障状态变化时广播 */
#define BMS_EVT_FAULT_CAN BIT(8)
#define BMS_EVT_FAULT_AUX BIT(9)

#endif
