#ifndef APPCONFIG_H
#define APPCONFIG_H

/*
 * 应用层配置集中入口(业务参数的唯一驻地)。
 *
 * 双轨配置约定:
 * - devicetree(app.overlay)只放硬件事实:引脚、地址、时钟、芯片参数。
 * - appconfig.h 只放业务参数:阈值、周期、优先级、功能开关。
 * 板级事实统一保存在 docs/board-facts,修改阈值时需同步核对。
 */

/* ---- 功能开关:七任务 + 调试诊断 ---- */
#define BMS_APP_ENABLE_FAULT_PROTECTION 1
#define BMS_APP_ENABLE_CHARGE_STATE 1
#define BMS_APP_ENABLE_SOC_ENERGY 1
#define BMS_APP_ENABLE_BALANCE 1
#define BMS_APP_ENABLE_CAN_COMM 1
#define BMS_APP_ENABLE_AUXILIARY 1
#define BMS_APP_ENABLE_COMM_DIAG 1

/*
 * 看门狗默认关:Renode 冒烟平台的 IWDG 模型行为未验证,误触发会把
 * 整个仿真复位。真机 bring-up 时置 1,并同步打开 prj.conf 的 CONFIG_WATCHDOG。
 */
#define BMS_APP_ENABLE_WATCHDOG 1 /* 2 s IWDG; feed requires healthy heartbeats */

/* ---- 周期与等待 ---- */
#define BMS_APP_SAMPLE_PERIOD_MS 100U
#define BMS_APP_COMM_EVENT_WAIT_TIMEOUT_MS 50U
#define BMS_APP_CAN_TX_PERIOD_MS 100U
#define BMS_APP_AUX_PERIOD_MS 500U

/*
 * ---- 线程栈:按实测高水位定尺,显式列出(F103C8 只有 20KB SRAM) ----
 *
 * 定尺规则:取插桩构建(带 CONFIG_BMS_TEST_TELEMETRY,栈用量的最坏情形)实测
 * 高水位 peak,栈 = 大于等于 peak/0.6 的最小 128 字节倍数、下限 512 —— 即测得
 * 峰值处保持 >=40% 余量。插桩会抬高栈用量,故生产固件(无插桩)只会更宽裕。
 * ISR/idle 栈是 Zephyr 配置项(实测各 ~20%),不在此列。
 * 实测值及当前结论记录在本文件中,后续需用线程分析器重新核验。
 *
 *  任务          peak(B)  栈(B)  峰值占用
 *  sample         388     768    51%
 *  fault          364     640    57%
 *  charge_state   348     640    54%
 *  balance        324     640    51%
 *  soc_energy     260     512    51%
 *  can_comm       444     768    58%
 *  comm_diag      332     640    52%
 *  auxiliary      588    1024    57%   (原 640 -> 91% 高危,遥测建 JSON 吃栈)
 */
#define BMS_APP_SAMPLE_STACK_SIZE 768U
#define BMS_APP_FAULT_STACK_SIZE 640U
#define BMS_APP_CHARGE_STATE_STACK_SIZE 640U
#define BMS_APP_BALANCE_STACK_SIZE 640U
#define BMS_APP_SOC_ENERGY_STACK_SIZE 512U
#define BMS_APP_CAN_COMM_STACK_SIZE 768U
#define BMS_APP_COMM_DIAG_STACK_SIZE 640U
#define BMS_APP_AUXILIARY_STACK_SIZE 1024U

/*
 * Zephyr 优先级规则:数字越小优先级越高。
 * 保护最高;辅助任务故意最低——任何高优先级任务失控饿死它,
 * 看门狗就得不到喂狗,这本身就是系统级健康检查。
 */
#define BMS_APP_FAULT_PRIORITY 0
#define BMS_APP_SAMPLE_PRIORITY 2
#define BMS_APP_CHARGE_STATE_PRIORITY 3
#define BMS_APP_BALANCE_PRIORITY 4
#define BMS_APP_SOC_ENERGY_PRIORITY 5
#define BMS_APP_CAN_COMM_PRIORITY 6
#define BMS_APP_COMM_DIAG_PRIORITY 7
#define BMS_APP_AUXILIARY_PRIORITY 8

/*
 * ---- 保护阈值 ----
 * 电压阈值与回差取自 docs/board-facts/bq76940_config.json 的 protection 节
 * (vendor 软件保护口径)。UV 的 vendor 回差为 0,靠恢复持续时间抑制抖动。
 */
#define BMS_APP_CELL_OV_MV 4200U
#define BMS_APP_CELL_OV_RECOVER_MV 4100U
#define BMS_APP_CELL_UV_MV 2800U
#define BMS_APP_CELL_UV_RECOVER_MV 2800U

/*
 * 温度窗口按充/放电分开,取自场景板级事实(charge 0..55C,discharge -20..70C)。
 * 单位 0.01 摄氏度;恢复回差 2.00 摄氏度。
 */
#define BMS_APP_CHG_OT_CDEG 5500
#define BMS_APP_CHG_UT_CDEG 0
#define BMS_APP_DSG_OT_CDEG 7000
#define BMS_APP_DSG_UT_CDEG (-2000)
#define BMS_APP_TEMP_RECOVER_HYST_CDEG 200

/*
 * 参与保护/均衡判定的温度通道数:板卡当前只装 TS1 一颗 NTC,
 * TS2 通道保留观测(快照与诊断仍输出两路)。没接 NTC 的通道
 * 读数是 0.00C,若纳入判定会永久误触发充电低温保护。
 */
#define BMS_APP_PROT_TEMP_CHANNELS 1U

/*
 * 过流阈值:BoardFacts software_oc_ma = 5000(vendor 只给一个值,
 * 充/放电先取对称口径)。电流约定:放电为正,充电为负。
 */
#define BMS_APP_CURRENT_OCD_MA 5000
#define BMS_APP_CURRENT_OCC_MA (-5000)
#define BMS_APP_CURRENT_OC_RECOVER_MA 4500

/*
 * 去抖与恢复时间。触发要求越限持续 trip_ms;恢复要求回到回差以内持续
 * recover_ms。过流反应更快。注意:trip_ms 必须远小于 Renode 冒烟场景的
 * 虚拟时长,否则断言看不到故障置位。
 */
#define BMS_APP_PROT_TRIP_MS 300U
#define BMS_APP_PROT_OC_TRIP_MS 100U
#define BMS_APP_PROT_RECOVER_MS 500U
#define BMS_APP_PROT_AFE_COMM_FAIL_LIMIT 3U

/* ---- 充放电状态识别:静置带 ± 滞回,消除模式抖动 ---- */
#define BMS_APP_CURRENT_IDLE_THRESHOLD_MA 200
#define BMS_APP_CURRENT_IDLE_HYST_MA 50

/*
 * ---- 能量估算 ----
 * 单位对齐 04-03/TC-04 规格:SOC/SOH/可信度用 0.1%(1000 = 100.0%),
 * 容量 mAh。SOC 路线 = OCV(最小单体)初始化 + 库仑积分 + 静置校准 +
 * 满充/放空边界重标定 + 采样无效冻结。
 */
#define BMS_APP_RATED_CAPACITY_MAH 20000U
#define BMS_APP_OCV_REST_CURRENT_MA 200
#define BMS_APP_OCV_REST_BLEND_PERMILLE 20U
#define BMS_APP_SOC_FULL_MIN_CELL_MV 4150U   /* 最小单体≥此 → 满充边界拉近 100% */
#define BMS_APP_SOC_EMPTY_MIN_CELL_MV 2900U  /* 最小单体≤此 → 放空边界拉近 0% */
#define BMS_APP_SOC_BOUND_BLEND_PERMILLE 50U /* 边界重标定步进 */
#define BMS_APP_SOC_MAX_INTEGRATE_DT_MS 5000U /* dt 超此视为断档,不积分 */
#define BMS_APP_SOC_CONF_CALIBRATED 600U     /* 静置校准后可信度 0.1% */
#define BMS_APP_SOC_CONF_INTEGRATING 400U    /* 仅库仑积分时可信度 */
#define BMS_APP_SOC_CONF_FROZEN 100U         /* 采样无效冻结时可信度 */

/* ---- 被动均衡 ----(thresholds track tests/scenarios balance data) */
#define BMS_APP_BALANCE_DELTA_ON_MV 60U
#define BMS_APP_BALANCE_DELTA_OFF_MV 25U
#define BMS_APP_BALANCE_MIN_CELL_MV 3800U
/*
 * 均衡允许的电流上限:超过就停(高电流下电芯电压被 IR 抬高、判据发飘)。
 * 原 500 太保守——正常充电(几百 mA)就把均衡关了,高单体一路领跑先撞 OV、越充越歪。
 * 抬到 2000mA:正常充放电流期间都边充边均衡(charge-balancing,行业常规),只在
 * 接近过流(OCD/OCC 5000mA)的真·大电流时才停。
 */
#define BMS_APP_BALANCE_MAX_ABS_CURRENT_MA 2000

/* ---- SOP 轻量限流规则表上限 ---- */
#define BMS_APP_SOP_MAX_CHARGE_MA 10000U
#define BMS_APP_SOP_MAX_DISCHARGE_MA 20000U

/* ---- 辅助任务 ---- */
#define BMS_APP_HEARTBEAT_STALE_MS 1000U
#define BMS_APP_WDT_TIMEOUT_MS 2000U

/*
 * 状态灯(PB13,led_status):按电量指示。SOC < 此阈值 = 电量不足 → 5Hz 快闪;
 * 电量充足 → 1Hz 慢闪。故障(latched)也快闪告警,优先级高于电量。单位 0.1%。
 */
#define BMS_APP_LED_LOW_SOC_0P1PCT 200U /* 20.0% */

#endif
