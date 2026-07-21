#ifndef BMS_DIAG_FORMAT_H
#define BMS_DIAG_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "core/bms_sample_snapshot.h"
#include "core/bms_energy_state.h"
#include "core/bms_fault_state.h"
#include "core/bms_balance_state.h"
#include "core/bms_power_state.h"
#include "core/bms_selftest_state.h"

/*
 * ============================================================================
 * 诊断文本编码(把状态结构编成一行 SNAP / 一行 AFE 寄存器转储)
 * ============================================================================
 *
 * 与 bms_protocol_codec(CAN 二进制编码)对称:这里是 **UART 诊断的文本编码**。
 * 纯函数、不碰任何硬件、不认 UART——只把状态结构 -> 文本写进调用方给的缓冲。
 * 于是编码逻辑可在主机侧单测,comm_diag_task 只剩"读仓库 + 发送 + 收命令"。
 *
 * 所有写入都在缓冲边界内逐字节夹取(满了就截断,绝不越界)。
 */

/*
 * 一次 SNAP 要展示的各状态视图。sample 必填(SNAP 的主体);其余为 NULL 表示
 * 该段仓库没读到,跳过不输出——完全复刻 comm_diag 逐仓库读、读到才拼的行为。
 */
typedef struct BmsSnapshotView {
	const BmsSampleSnapshot *sample;   /* 必填 */
	const BmsEnergyState *energy;      /* NULL 则跳过 soc/soh/... 段 */
	const BmsFaultState *fault;        /* NULL 则跳过 fault_bits 段 */
	const BmsBalanceState *balance;    /* NULL 则跳过 bal_* 段 */
	const BmsPowerState *power;        /* NULL 则跳过 pwr_mode/chg/dsg 段 */
	const BmsSelftestState *selftest;  /* NULL 则跳过 hb_stale 段 */
} BmsSnapshotView;

/*
 * 把一行 "SNAP version=... \r\n" 写进 buf(有界),返回写入字节数(<= buf_size)。
 * view 或 view->sample 为 NULL 时不写、返回 0。可重入、线程安全(无静态状态)。
 */
size_t bms_diag_format_snapshot(char *buf, size_t buf_size, const BmsSnapshotView *view);

/*
 * 格式化一行 AFE 寄存器转储:rc==0 -> "AFE_REG addr=XX value=XX\r\n";
 * 否则 -> "AFE_REG_ERR addr=XX rc=<n>\r\n"。返回写入字节数(<= buf_size)。
 */
size_t bms_diag_format_afe_reg(char *buf, size_t buf_size, uint8_t addr, int rc, uint8_t value);

#endif /* BMS_DIAG_FORMAT_H */
