#ifndef BMS_STATUS_LED_H
#define BMS_STATUS_LED_H

#include <stdint.h>

/*
 * ============================================================================
 * 状态指示灯(把"状态等级 -> 灯的表现"这段策略从 AuxiliaryTask 里拆出来)
 * ============================================================================
 *
 * AuxiliaryTask 只负责"算出系统当前是什么状态等级",怎么把它渲染成灯(慢闪/快闪/
 * 呼吸)是本模块的事。后端在**编译期二选一**(和 comm_diag 的 UART DMA/poll 同款
 * hw/sim 分策略,是嵌入式惯用的编译期 Strategy):
 *   - 默认:GPIO 闪烁——Renode 可建模,仿真冒烟能验,真机也能用。
 *   - CONFIG_PWM:PWM 呼吸——真机 D3=PA15=TIM2_CH1,低有效由 DT PWM 反相处理。
 * 换后端不动 aux:aux 只调 init/render。本模块自持指示灯设备(drivers 层可直接
 * 用 DT,与 bq769x0 驱动自持 I2C 同理),故 aux 不再注入 LED 句柄。
 */

typedef enum {
	BMS_INDICATOR_NORMAL = 0, /* 健康:慢(GPIO 慢闪 / PWM 慢呼吸) */
	BMS_INDICATOR_ALERT = 1,  /* 故障或低电:快(快闪 / 快呼吸)告警 */
} BmsIndicatorState;

/* 配置并复位指示灯。成功 0,设备不可用 -1。调度器启动后调用。 */
int bms_status_led_init(void);

/*
 * 按状态 + 当前时间(k_uptime_get_32)渲染一次,由 aux 周期调用。
 * 返回当前亮度档(GPIO:0/1;PWM:0..255 粗档),供上层遥测。
 */
int bms_status_led_render(BmsIndicatorState state, uint32_t now_ms);

#endif /* BMS_STATUS_LED_H */
