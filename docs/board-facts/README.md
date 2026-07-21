# 板级事实资料

本目录是固件、Renode 和上位机共同使用的板级事实来源，记录从厂家发货工程、
原理图和现有程序中整理出的 MCU 引脚、BQ76940 参数、CAN 字段与硬件连接关系。

## 文件说明

| 文件 | 作用 |
|---|---|
| [`bms_board_facts.md`](bms_board_facts.md) | 给开发者阅读的板级说明、来源、关键判断和仿真假设 |
| [`pinmap.json`](pinmap.json) | MCU 引脚、外设和板级网络的结构化映射 |
| [`bq76940_config.json`](bq76940_config.json) | AFE 寄存器、测量换算、NTC 与保护参数 |
| [`can_protocol.json`](can_protocol.json) | 应用 CAN 上报字段，供固件与虚拟上位机核对 |

## 当前硬件口径

- MCU：STM32F103C8T6；
- AFE：BQ76940，物理上保留 `VC1..VC15` 口径；
- 实接电芯：9 串；
- 有效 VC 槽位：`1、2、5、6、7、10、11、12、15`；
- BQ 通信：PB8/PB9 软件开漏 I2C，地址 `0x08`；
- CAN：PA11/PA12，500 kbit/s 扩展帧；
- 诊断串口：USART1 PA9/PA10，115200；
- 状态灯：D3/PA15，低有效。

固件算法只看到 9 个逻辑电芯，
[`firmware/app.overlay`](../../firmware/app.overlay) 的 `cell-channels` 负责把逻辑电芯
映射到 15 个物理 VC 槽位。CAN 仍按厂家 15 槽位格式发送，未接槽位填 0。

## 使用规则

1. 硬件引脚、地址或电芯映射变化时，先更新本目录，再修改 devicetree 和 Renode。
2. 保护阈值变化时，同时核对
   [`firmware/src/config/appconfig.h`](../../firmware/src/config/appconfig.h) 与测试场景。
3. CAN 字段变化时，同步更新
   [`firmware/src/algo/bms_protocol_codec.c`](../../firmware/src/algo/bms_protocol_codec.c)、
   Python 上位机和 Renode 断言。
4. JSON 保存机器可读事实，`bms_board_facts.md` 记录来源、推导和仍待真机确认的假设。

原始厂家资料未纳入本公开仓库；本目录保存的是可公开、可复核的整理结果。
