#include "indicator/bms_status_led.h"

#include <zephyr/kernel.h>

/*
 * 闪烁/呼吸周期(ms):告警快、正常慢。GPIO 用半周期翻转,PWM 用整周期三角波。
 */
#define LED_BLINK_HALF_ALERT_MS 100U    /* 5Hz 快闪 */
#define LED_BLINK_HALF_NORMAL_MS 500U   /* 1Hz 慢闪 */
#define LED_BREATHE_PERIOD_ALERT_MS 800U
#define LED_BREATHE_PERIOD_NORMAL_MS 4000U
#define LED_BREATHE_STEPS 255U          /* 亮度档 0..255 */

#if defined(CONFIG_PWM)
/* ---- PWM 呼吸后端(真机):PA15 = TIM2_CH1,低有效由 DT PWM_POLARITY_INVERTED 处理 ---- */
#include <zephyr/drivers/pwm.h>

static const struct pwm_dt_spec s_pwm = PWM_DT_SPEC_GET(DT_ALIAS(status_led_pwm));

int bms_status_led_init(void)
{
	if (!pwm_is_ready_dt(&s_pwm)) {
		return -1;
	}
	return pwm_set_pulse_dt(&s_pwm, 0); /* 起始熄灭 */
}

int bms_status_led_render(BmsIndicatorState state, uint32_t now_ms)
{
	uint32_t period = (state == BMS_INDICATOR_ALERT) ? LED_BREATHE_PERIOD_ALERT_MS
							 : LED_BREATHE_PERIOD_NORMAL_MS;
	uint32_t half = period / 2U;
	uint32_t phase = now_ms % period;
	/* 三角波亮度:上半周升、下半周降 */
	uint32_t bright = (phase < half) ? (phase * LED_BREATHE_STEPS / half)
					 : (LED_BREATHE_STEPS - (phase - half) * LED_BREATHE_STEPS / half);
	uint32_t pulse = (uint32_t)((uint64_t)s_pwm.period * bright / LED_BREATHE_STEPS);

	(void)pwm_set_pulse_dt(&s_pwm, pulse);
	return (int)bright;
}

#else
/* ---- GPIO 闪烁后端(仿真 + 默认):Renode 可建模,冒烟能验 ---- */
#include <zephyr/drivers/gpio.h>

static const struct gpio_dt_spec s_led =
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(led_status), gpios, {0});

int bms_status_led_init(void)
{
	if ((s_led.port == NULL) || !gpio_is_ready_dt(&s_led)) {
		return -1;
	}
	return gpio_pin_configure_dt(&s_led, GPIO_OUTPUT_INACTIVE);
}

int bms_status_led_render(BmsIndicatorState state, uint32_t now_ms)
{
	uint32_t half = (state == BMS_INDICATOR_ALERT) ? LED_BLINK_HALF_ALERT_MS
						       : LED_BLINK_HALF_NORMAL_MS;
	int level;

	if (s_led.port == NULL) {
		return 0;
	}
	level = (int)((now_ms / half) & 1U);
	(void)gpio_pin_set_dt(&s_led, level);
	return level;
}
#endif
