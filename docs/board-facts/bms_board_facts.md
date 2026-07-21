# BMS 板级事实整理

本文记录从厂家 BQ76940 发货资料中提取出的板级事实，供 `tests`、Renode、虚拟上位机和后续固件重构使用。

## 资料来源

主要来源：

```text
Reference/1.BQ76940发货资料/BQ76940发货资料 20220817/
  3.程序/1.BQ76940（程序加CAN20211123）改15串/BMS_s940
```

当前仓库中的对照源码：

```text
legacy vendor source tree: BMS_s940
```

主要代码入口：

- `Project/main.c`
- `BSP/BQ76930.c`
- `BSP/BQ76930.h`
- `BSP/i2c1.c`
- `BSP/i2c1.h`
- `BSP/can.c`
- `BSP/can.h`
- `BSP/IO_CTRL.*`
- `BSP/led.h`

## 工程变体

| 项目 | 当前事实 |
|---|---|
| 当前使用的厂家变体 | BQ76940，程序加 CAN，20211123，15 串版本 |
| 厂家 MDK 主工程目标器件 | `STM32F103C6` |
| 厂家代码处理的 AFE 采样口 | 15 路，对应 BQ76940 `VC1..VC15` 和 `Batteryval[0]` 到 `Batteryval[14]` |
| 实物有效串数 | 9 路逻辑串联电芯；以厂家发货源码为准，实际检查的 VC 槽位为 `VC1,VC2,VC5,VC6,VC7,VC10,VC11,VC12,VC15` |
| 当前项目有效业务电芯 | 算法和 Zephyr 快照按 `BMS_SAMPLE_CELL_COUNT=9` 处理，并通过映射表读上述 9 个 VC 槽位 |
| 总压计算 | 厂家示例按 `Batteryval[0]` 到 `Batteryval[14]` 求和；当前 Zephyr 业务按 9 个逻辑电芯求和 |
| 当前测试平台口径 | 按 15 个 BQ76940 AFE 口建模，场景文件用 `active_cell_channels` 标出 9 个实接电芯槽位；未接 VC 槽位在 9 串场景中为 0 |

## MCU 与运行方式

| 项目 | 当前事实 |
|---|---|
| MCU | STM32F103C6，厂家 MDK 主工程和当前 GCC 工程均按 C6 口径；若实物为 C8，可视为同封装同引脚的容量放大版本 |
| 当前库风格 | STM32F10x StdPeriph 标准库 |
| 主循环 | 裸机循环，包含 SysTick、看门狗、CAN 轮询、采样、保护和上报 |
| 周期来源 | `TIM2_Config(99,7199)`，厂家注释为 100 ms 定时器中断 |
| 看门狗 | `IWDG_Init(6,1250)`，厂家注释约 4 s |

## BQ76940 与 IIC

| 项目 | 当前事实 |
|---|---|
| BQ 总线初始化函数 | `I2C1_Configuration()` |
| IIC 方式 | GPIO 开漏软件模拟 IIC |
| SCL | PB8 |
| SDA | PB9 |
| BQ 7 位地址 | `0x08` |
| 写地址字节 | `0x10` |
| 读地址字节 | `0x11` |
| 写 CRC | BQ 寄存器写入带 CRC |
| CRC 多项式 | `0x07` |
| 读 CRC | 厂家代码未使用 |
| BQ 唤醒脚 | PA8，名称 `MCU_WAKE_BQ` |
| BQ ALERT 相关脚 | PB2，名称 `MCU_BQ1_ART`，但当前代码配置为输出，需结合原理图复核 |
| 9 串实接映射 | 逻辑电芯 1..9 对应 `VC1,VC2,VC5,VC6,VC7,VC10,VC11,VC12,VC15` |

重要 BQ 寄存器：

| 寄存器 | 地址 | 用途 |
|---|---:|---|
| `SYS_STAT` | `0x00` | 故障状态和写 1 清除 |
| `CELLBAL1..3` | `0x01..0x03` | 被动均衡控制 |
| `SYS_CTRL1` | `0x04` | ADC、ship、wake 相关控制 |
| `SYS_CTRL2` | `0x05` | CHG/DSG FET 控制位 |
| `PROTECT1..3` | `0x06..0x08` | SCD/OCD/UV/OV 保护配置 |
| `OV_TRIP` | `0x09` | 硬件过压阈值 |
| `UV_TRIP` | `0x0A` | 硬件欠压阈值 |
| `CC_CFG` | `0x0B` | 库仑计配置 |
| `VC1..VC15` | `0x0C..0x29` | 单体电压高低字节 |
| `BAT` | `0x2A..0x2B` | 包电压寄存器 |
| `TS1` | `0x2C..0x2D` | 温度通道 |
| `CC` | `0x32..0x33` | 电流或库仑计 raw 值 |
| `ADCGAIN1` | `0x50` | ADC gain 校准 |
| `ADCOFFSET` | `0x51` | ADC offset 校准 |
| `ADCGAIN2` | `0x59` | ADC gain 校准 |

## CAN

| 项目 | 当前事实 |
|---|---|
| 外设 | CAN1 |
| TX | PA12 |
| RX | PA11 |
| main 中模式 | `CAN_Mode_Normal` |
| 标称波特率 | 500 kbit/s |
| 初始化调用 | `CAN_Mode_Init(CAN_SJW_1tq, CAN_BS2_8tq, CAN_BS1_9tq, 4, CAN_Mode_Normal)` |
| 帧格式 | 扩展 ID 数据帧 |
| 接收过滤器 | Filter 0，32 位 mask，ID/mask 全 0，接收到 FIFO0 |
| 接收中断 | `CAN_RX0_INT_ENABLE 0`，即关闭 |

厂家代码上报 7 个 8 字节扩展帧。帧 1-5 保持 BQ 物理 `VC1..VC15` 槽位顺序；9 串实接场景下，`VC3,VC4,VC8,VC9,VC13,VC14` 这些未接槽位应为 0，而不是把 9 个逻辑电芯压紧到前 3 帧。

| 扩展 ID | Byte0 | Byte1 | 内容 |
|---:|---:|---:|---|
| `0x0001` | `0xAA` | `0x01` | 第 1 到 3 节电压，大端 mV |
| `0x0002` | `0xAA` | `0x02` | 第 4 到 6 节电压，大端 mV |
| `0x0003` | `0xAA` | `0x03` | 第 7 到 9 节电压，大端 mV |
| `0x0004` | `0xAA` | `0x04` | 第 10 到 12 节电压，大端 mV |
| `0x0005` | `0xAA` | `0x05` | 第 13 到 15 节电压，大端 mV |
| `0x0006` | `0xAA` | `0x06` | 总压、SOC、电流 |
| `0x0007` | `0xAA` | `0x07` | 温度、DSG 状态、CHG 状态 |

## PA12 判定

结论：PA12 在板级事实和 CubeMX 工程里应按 CAN1_TX 处理，不再标为真实引脚冲突。

依据如下：

- `Project/main.c` 的初始化顺序是 `IO_CTRL_Config()` 先执行，随后立即执行 `CAN_Mode_Init(...)`。
- `BSP/IO_CTRL.h` 把 `MCU_KZ_P2` 定义为 `GPIO_Pin_12`，`BSP/IO_CTRL.c` 中只在初始化阶段把它配置为普通推挽输出并拉低一次。
- `BSP/can.c` 中 `CAN_Mode_Init()` 随后把 PA12 重新配置为 `GPIO_Mode_AF_PP`，即 CAN1_TX；PA11 配置为 CAN1_RX。
- 全局搜索发货源码后，`MCU_KZ_P2_ONOFF` 只出现在 `IO_CTRL` 初始化里，主循环和 BQ/CAN 逻辑没有继续驱动 PA12。

因此这里不是板子硬件冲突，而是厂家源码保留了一个过期 GPIO 宏和启动阶段临时初始化。测试平台、Renode 和 CubeMX 均以 `CAN1_TX = PA12`、`CAN1_RX = PA11` 为准；`MCU_KZ_P2` 仅作为 legacy alias 记录，不作为可用 GPIO 输出建模。

## UART

| 项目 | 当前事实 |
|---|---|
| 调试串口 | USART1 |
| USART1 引脚 | PA9 TX，PA10 RX |
| USART1 波特率 | 115200 |
| 显示或蓝牙侧串口 | USART2，main 中初始化为 9600 |
| USART2 引脚 | PA2 TX，PA3 RX |
| 额外串口函数 | `usart2.c` 中还有 USART1/2/3 的配置和打印函数；`USART3_Config()` 未被 main 调用，且函数内未配置 USART3 GPIO |

## GPIO

| 信号 | 引脚 | 代码方向 | 备注 |
|---|---|---|---|
| `MCU_KZ_QB_POWER` | PA0 | 推挽输出 | 电源控制，具体电路待确认 |
| `MCU_D_POWER` | PA1 | 推挽输出 | 电源控制，具体电路待确认 |
| `MCU_WAKE_BQ` | PA8 | 推挽输出 | 唤醒 BQ，代码中高低脉冲 |
| `MCU_KZ_P2` | PA12 | legacy 输出初始化，随后被 CAN 初始化覆盖 | 不作为真实 GPIO 输出建模；最终所有者是 CAN1_TX |
| `MCU_IN_STA1` | PB12 | 推挽输出 | 初始化时置高 |
| `STA_WAKE` | PB1 | 推挽输出 | 唤醒或状态脚，待确认 |
| `MCU_BQ1_ART` | PB2 | 推挽输出 | 名称像 ALERT，但代码配置为输出 |
| `PB3` | PB3 | 推挽输出 | 使用前关闭 JTAG |
| `PB4` | PB4 | 推挽输出 | 使用前关闭 JTAG |
| `PB5` | PB5 | 推挽输出 | 输出 |
| LED1 | PA15 | 输出 | 电量或状态 LED |
| LED2 | PB13 | 输出 | 电量或状态 LED |
| LED3 | PB14 | 输出 | 电量或状态 LED |
| LED4 | PB15 | 输出 | 电量或状态 LED |

## 保护与测量参数

| 项目 | 值 | 来源 |
|---|---:|---|
| 软件过压阈值 | 4200 mV | `main.c` |
| 软件过压恢复 | 4100 mV | `main.c` |
| 软件欠压阈值 | 2800 mV | `main.c` |
| 软件欠压恢复 | 大于 2800 mV | `main.c` |
| 软件过流阈值 | 5000 mA | `main.c` |
| 硬件过压目标 | 4400 mV | `BQ76930.h` |
| 硬件过压恢复 | 4300 mV | `BQ76930.h` |
| 硬件欠压目标 | 2400 mV | `BQ76930.h` |
| 硬件欠压恢复 | 2500 mV | `BQ76930.h` |
| 温度保护参数 | 存在 Flash 地址 `0x08007C00` | `main.c` |
| 电流换算 | 正向分支中 `Currentval = raw * 2.11` mA | `BQ76930.c` |
| NTC 模型 | 10k NTC，B=3380，25 摄氏度参考 | `BQ76930.c` |
| NTC ADC LSB | 382 uV | `BQ76930.c` |

### 电流符号约定(重要)

这块板的 SRP/SRN 接法让 **BQ 的 CC 寄存器"充电为正"**。厂家固件(`Get_BQ_Current`)只取
**幅值**显示("当前电流为:X mA")、不带符号,充放方向敏感的保护交给 BQ 芯片硬件(SYS_STAT),
所以厂家不受符号影响。

本工程(Zephyr)引入了**带符号电流**(全局约定 **放电为正、充电为负**)+ 充放电状态机,
因此驱动 `bq769x0_read_current` **对 CC 取反**,把"充电为正"翻成固件约定的"放电为正"。
仿真 mock(`bq76940_i2c.py:_current_ma_to_raw`)据此把 CC 编码成**充电为正**,与真机对齐、
并与该取反round-trip(冒烟场景/断言仍按放电为正书写,不受影响)。

真机佐证:接充电器时驱动读到**负**电流、`pwr_mode` 判为充电;修正前(未取反)会读成正、误判放电。

## 对测试平台的含义

- Renode 平台应使用 STM32F103 基础模型，并补充 BMS 板级外设。
- 虚拟 BQ76940 需要支持地址 `0x08`、CRC 写和无 CRC 单字节读。
- CAN 协议应按 7 个扩展 ID 数据帧解码。
- 场景文件固定提供 15 路 AFE 口采样值，和 BQ76940 寄存器 `VC1..VC15`、厂家 CAN 7 帧上报保持一致；`active_cell_channels=[1,2,5,6,7,10,11,12,15]` 标出 9 个实接电芯通道。
- PA12 不再按 `MCU_KZ_P2` 输出建模；它的最终外设复用是 CAN1_TX。CubeMX 工程也应按 CAN1 配置 PA11/PA12。
