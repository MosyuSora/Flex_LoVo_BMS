# 上位机

上位机计划并行维护 Python 与 Qt 两种方案：

| 目录 | 状态 | 定位 |
|---|---|---|
| [`python/`](python/) | 已提供 | 快速调试、协议验证、自动化和无实物联调 |
| `qt/` | 规划中 | 面向长期使用的原生桌面应用 |

当前发布版本只包含 Python 方案。安装依赖：

```powershell
python -m pip install -r .\host\python\requirements.txt
```

连接实物 USART1：

```powershell
.\host\python\run_uart_upper.ps1 -Port COM3 -Baud 115200
```

不连接实物，直接用 Renode 运行虚拟上位机：

```powershell
.\host\python\run_virtual_upper.ps1
```

功能、输入输出和测试方法见 [Python 上位机说明](python/README.md)。

后续 Qt 版本应复用相同的协议定义和板级事实，不在界面层重新定义 CAN/UART 字段。
协议来源为 [`../docs/board-facts/can_protocol.json`](../docs/board-facts/can_protocol.json)
以及固件 [`bms_protocol_codec.c`](../firmware/src/algo/bms_protocol_codec.c)。
