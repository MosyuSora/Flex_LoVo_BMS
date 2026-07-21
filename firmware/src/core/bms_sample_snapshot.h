#ifndef BMS_SAMPLE_SNAPSHOT_H
#define BMS_SAMPLE_SNAPSHOT_H

#include <stdint.h>

/*
 * BQ76940 物理上有 VC1..VC15 共 15 个采样口;发货源码实际只把
 * VC1/VC2/VC5/VC6/VC7/VC10/VC11/VC12/VC15 作为 9 个有效串联电芯。
 * 快照只暴露逻辑电芯,算法按 9 处理;需要恢复厂家 CAN 槽位时使用
 * BMS_SAMPLE_CELL_CHANNEL_MAP。
 *
 * 快照是纯测量值:不含 SOC 等算法输出(算法结果在 BmsEnergyState 里,
 * 谁需要谁去状态仓库读),避免采样任务反向依赖估算任务。
 */
#define BMS_SAMPLE_CELL_COUNT 9U
#define BMS_SAMPLE_PHYSICAL_CELL_COUNT 15U
#define BMS_SAMPLE_CELL_CHANNEL_MAP { 1U, 2U, 5U, 6U, 7U, 10U, 11U, 12U, 15U }
#define BMS_SAMPLE_TEMP_COUNT 2U

typedef struct BmsSampleSnapshot {
	uint32_t version;
	uint32_t timestamp_ms;
	uint16_t cell_mv[BMS_SAMPLE_CELL_COUNT];
	uint16_t pack_mv;
	int16_t current_ma; /* 放电为正,充电为负 */
	int16_t temperature_cdeg[BMS_SAMPLE_TEMP_COUNT];
	uint8_t temp_valid; /* 第 i 位 = 温度通道 i 可信(NTC 未开短路) */
	uint8_t flags;
} BmsSampleSnapshot;

#endif
