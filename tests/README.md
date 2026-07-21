# 测试与仿真

本目录保存不依赖实物板的回归测试资产。主要测试平台是 Renode：它运行真实的
Zephyr ELF，并用虚拟 STM32F103、BQ76940、CAN、UART 和看门狗验证启动、采样、
保护、均衡、协议与调度行为。

## 目录结构

| 目录 | 内容 |
|---|---|
| [`renode/`](renode/) | Renode 平台描述、外设模型、运行脚本和断言 |
| [`scenarios/`](scenarios/) | 电芯电压、电流、温度和 NTC 参数场景 |
| `reports/` | 上位机生成的报告，运行时创建，不纳入 Git |

## 快速开始

安装 Renode 后设置路径；默认安装在
`C:\Program Files\Renode\bin\Renode.exe` 时可省略环境变量。

```powershell
$env:BMS_RENODE_PATH = "D:\Tools\Renode\bin\Renode.exe"
$env:ZEPHYR_BASE = "C:\zephyr-workspace\zephyr"
$env:GNUARMEMB_TOOLCHAIN_PATH = "C:\Tools\xpack-arm-none-eabi-gcc-15.2.1-1.1"
```

从仓库根目录运行：

```powershell
# 主场景冒烟
.\tests\renode\run_zephyr_bms_smoke.ps1

# 串口命令路径
.\tests\renode\run_zephyr_uart_rx_test.ps1

# 看门狗复位
.\tests\renode\run_zephyr_wdt_reset_test.ps1

# 栈、节拍、调度和 CAN 压力
.\tests\renode\run_zephyr_stress_test.ps1
```

脚本默认先构建测试固件。已有正确的 `firmware/build/zephyr/zephyr.elf` 时可对支持
该参数的脚本传入 `-SkipBuild`，缩短调试循环。

## 场景文件

[`scenarios/`](scenarios/) 中每个 JSON 表示一组稳定的电池包输入。最小结构参考
[`normal_15s_shipping.json`](scenarios/normal_15s_shipping.json)：

```json
{
  "name": "normal_15s_shipping_pack",
  "cell_count": 15,
  "active_cell_channels": [1, 2, 5, 6, 7, 10, 11, 12, 15],
  "cell_mv": [3650, 3652, 0, 0, 3651, 3649, 3653, 0, 0, 3650, 3651, 3652, 0, 0, 3650],
  "current_ma": 0,
  "temperatures_c": [25.0, 25.0]
}
```

新增场景时：

1. 保持 15 个 `cell_mv` 槽位，未接通道填 0；
2. 保持 `active_cell_channels` 与真实 9 串映射一致；
3. 电流约定为放电正、充电负；
4. 温度使用摄氏度；
5. 给场景取能说明预期的名称，例如 `charge_overtemp_15s_shipping.json`；
6. 在 smoke 脚本的场景表中加入文件、预期故障位和 FET/均衡断言。

现有场景覆盖正常、过压、欠压、均衡、NTC 开路和 NTC 短路。

## 新增测试的基本方法

1. 优先复用 [`renode/run_zephyr_uart_smoke.ps1`](renode/run_zephyr_uart_smoke.ps1)
   的构建、平台装载和日志收集逻辑。
2. 仅改变电池输入时新增场景 JSON；需要新外设行为时再修改
   [`renode/peripherals/`](renode/peripherals/)。
3. `.resc` 负责创建机器、装载 `.repl`、加载 ELF、连接模型和控制虚拟时间。
4. PowerShell 负责生成临时 `.resc`、启动 Renode、读取日志并执行最终断言。
5. 断言应检查可观察业务结果，例如故障位、FET 状态、CAN 字段或重启次数，
   不应只检查“进程退出码为 0”。
6. 新测试至少包含一个会在功能破坏时稳定失败的负向条件。

详细的平台结构、参数和示例见 [Renode 测试平台说明](renode/README.md)。

## 测试输出

Renode 运行日志、动态生成的 `.resc`、CAN 抓包和 JSON 报告写入
`tests/renode/artifacts/`；虚拟上位机报告写入 `tests/reports/`。两个目录都被
`.gitignore` 排除。

测试失败时应先保留对应日志，再区分是固件断言失败、平台模型限制、构建环境错误，
还是实际业务回归。

## 仿真边界

Renode 适合验证数字逻辑和软件行为，但不能替代：

- BQ76940 模拟测量精度和真实 CRC/I2C 时序裕量；
- CAN 收发器、电缆、终端电阻和 bus-off 物理故障；
- MOSFET、预充、功率回路、热设计与短路能量；
- HSE 晶振、DMA、EMC、掉电和 Flash 边界条件的真机测试。

涉及硬件安全边界的修改必须在仿真通过后继续做限流真机验证。
