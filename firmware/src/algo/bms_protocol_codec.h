#ifndef BMS_PROTOCOL_CODEC_H
#define BMS_PROTOCOL_CODEC_H

#include "algo/bms_command.h"
#include "core/bms_energy_state.h"
#include "core/bms_power_state.h"
#include "core/bms_sample_snapshot.h"

#include <stddef.h>
#include <stdint.h>

/*
 * CAN 应用协议编解码(纯函数,不含任何硬件头,可在主机侧单测)。
 *
 * TX layout = docs/board-facts/can_protocol.json:
 * 扩展数据帧,DLC 8,大端 16 位;byte0 = 0xAA 魔数,byte1 = 帧号。
 * 帧 1-5 电芯电压按厂家物理 VC1..VC15 槽位排布,未接槽位填 0;
 * 帧 6 包压/SOC/电流,帧 7 温度与 FET 状态。
 *
 * RX 是项目扩展(vendor 协议无 RX 定义):
 * 扩展帧 id=BMS_PROTOCOL_CMD_FRAME_ID,byte0=0xA5,byte1=命令码。
 */
#define BMS_PROTOCOL_FRAME_COUNT 7U
#define BMS_PROTOCOL_FRAME_DLC 8U
#define BMS_PROTOCOL_TX_MAGIC 0xAAU
#define BMS_PROTOCOL_CMD_FRAME_ID 0x0100U
#define BMS_PROTOCOL_CMD_MAGIC 0xA5U

typedef struct BmsProtocolFrame {
	uint32_t id; /* 扩展帧 ID */
	uint8_t dlc;
	uint8_t data[BMS_PROTOCOL_FRAME_DLC];
} BmsProtocolFrame;

/* frame_no 取 1..BMS_PROTOCOL_FRAME_COUNT,返回 0 成功 */
int bms_protocol_encode_frame(uint8_t frame_no,
			      const BmsSampleSnapshot *sample,
			      const BmsEnergyState *energy,
			      const BmsPowerState *power,
			      BmsProtocolFrame *out);

/* 返回 0 = 解析出有效命令 */
int bms_protocol_decode_command(const BmsProtocolFrame *in, BmsCommand *out);

#endif
