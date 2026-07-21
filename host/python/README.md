# Python 上位机

本目录包含两个可运行入口：连接实物 USART1 的 UART 实时界面，以及运行 Renode
固件回归并解码 CAN 遥测的虚拟上位机。二者用于研发与协议验证，不替代量产诊断工具。

## 环境与安装

需要 Python 3.10 或更高版本，当前已在 Python 3.13.5 上验证。

```powershell
python -m pip install -r .\host\python\requirements.txt
```

`requirements.txt` 当前只要求 `pyserial`。图形界面使用 Python 标准库 Tkinter；
如果自定义 Python 发行版没有 Tk 支持，需要补装对应组件。

## 文件说明

| 文件 | 作用 |
|---|---|
| [`uart_upper.py`](uart_upper.py) | UART 实时 GUI、曲线、日志和 CSV 保存 |
| [`uart_protocol.py`](uart_protocol.py) | SNAP 文本解析和字段模型 |
| [`virtual_bms_upper.py`](virtual_bms_upper.py) | 启动 Renode、收集 CAN 记录并生成报告 |
| [`test_uart_protocol.py`](test_uart_protocol.py) | UART 解析器单元测试 |
| [`run_uart_upper.ps1`](run_uart_upper.ps1) | UART GUI 启动包装 |
| [`run_virtual_upper.ps1`](run_virtual_upper.ps1) | 虚拟上位机启动包装 |

## UART 实时上位机

目标串口是固件 USART1：115200 baud、8 data bits、no parity、1 stop bit。

```powershell
.\host\python\run_uart_upper.ps1 -Port COM3 -Baud 115200
```

不指定 `-Port` 时，可在界面中刷新串口列表并选择设备：

```powershell
.\host\python\run_uart_upper.ps1
```

界面支持：

- 连接、断开和刷新串口；
- 手动发送 `S` 获取 SNAP；
- 周期请求和连续刷新；
- 解析 15 个 VC 槽位，其中 9 个实接槽位显示有效值；
- 显示包压、电流、温度、SOC、SOH、保护、均衡和 CHG/DSG 状态；
- 对过压、欠压和保护状态进行颜色提示；
- 显示包压趋势；
- 保存 SNAP CSV 和原始 UART 日志到 `tests/reports/`。

固件 UART 命令：

| 命令 | 返回 |
|---|---|
| `S` / `s` | 一帧 SNAP |
| `R` / `r` | BQ76940 `0x00..0x59` 寄存器转储 |
| 其他非空白字符 | `ERR command` |

如果界面连接成功但没有数据，先手动发送 `S`，再检查串口是否被其他程序占用、
波特率是否为 115200，以及固件是否输出启动 banner。

## 虚拟 CAN 上位机

虚拟上位机用于在没有 USB-CAN 和实物板时检查应用 CAN 协议：

```powershell
.\host\python\run_virtual_upper.ps1
```

它会执行以下流程：

1. 调用 [`../../tests/renode/run_zephyr_uart_smoke.ps1`](../../tests/renode/run_zephyr_uart_smoke.ps1)
   运行 `normal` 场景，并启用 Renode 1.16 的 CAN 初始化兼容处理；
2. 从场景生成的 UART 日志中收集 `BMS_CAN_TX` 记录，并兼容旧版
   `BMS_CAN_FRAME` 格式；
3. 按 `0x0001..0x0007` 协议解码最新一组帧；
4. 检查魔数、帧完整性和有效槽位；
5. 输出电芯、包压、SOC、电流、温度和 FET 状态；
6. 写入 `tests/reports/virtual_upper_latest.json`。

报告包含原始帧、解码值和错误列表，可用于固件重构前后的自动对比。

### 当前解码字段

| 字段 | CAN 来源 |
|---|---|
| 15 个 BQ76940 VC 槽位电压 | `0x0001..0x0005` |
| 包电压 | `0x0006` bytes 2..3 |
| SOC 百分比 | `0x0006` bytes 4..5 |
| 有符号电流 | `0x0006` bytes 6..7 |
| 温度 | `0x0007` byte 2 |
| DSG/CHG 原始状态 | `0x0007` bytes 3..4 |

协议结构来源为
[`../../docs/board-facts/can_protocol.json`](../../docs/board-facts/can_protocol.json)，固件编码
实现为 [`bms_protocol_codec.c`](../../firmware/src/algo/bms_protocol_codec.c)。修改协议时应
同步更新这两处、上位机解析器与测试断言。

## 运行单元测试

```powershell
python -m unittest discover -s .\host\python -p "test_*.py"
```

单元测试只验证 SNAP 文本解析，不启动 Renode。虚拟 CAN 上位机属于集成验证，
需要完整 Zephyr 与 Renode 环境。

## 输出目录

```text
tests/reports/
├── virtual_upper_latest.json
├── *.csv
└── *.log
```

该目录运行时自动创建且不纳入 Git。提交问题时可附上报告，但应先检查日志中是否
包含设备序列号、电脑路径或其他不适合公开的信息。

## 与 Qt 方案的边界

未来 Qt 上位机应把协议解析放在独立模块，复用相同字段、单位、错误定义和测试向量。
Python 版本继续承担脚本化回归、快速协议实验和 Renode 集成，不要求与 Qt 界面布局一致。
