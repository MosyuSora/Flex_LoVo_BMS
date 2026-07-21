#include "algo/bms_protocol_codec.h"

#include <string.h>

static void put_be16(uint8_t *dst, uint16_t value)
{
	dst[0] = (uint8_t)(value >> 8);
	dst[1] = (uint8_t)(value & 0xFFU);
}

static uint16_t sample_cell_at_vc_channel(const BmsSampleSnapshot *sample,
					  uint8_t vc_channel)
{
	static const uint8_t logical_to_vc[BMS_SAMPLE_CELL_COUNT] =
		BMS_SAMPLE_CELL_CHANNEL_MAP;

	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		if (logical_to_vc[i] == vc_channel) {
			return sample->cell_mv[i];
		}
	}

	return 0U;
}

int bms_protocol_encode_frame(uint8_t frame_no,
			      const BmsSampleSnapshot *sample,
			      const BmsEnergyState *energy,
			      const BmsPowerState *power,
			      BmsProtocolFrame *out)
{
	if ((sample == NULL) || (energy == NULL) || (power == NULL) ||
	    (out == NULL) || (frame_no < 1U) ||
	    (frame_no > BMS_PROTOCOL_FRAME_COUNT)) {
		return -1;
	}

	memset(out, 0, sizeof(*out));
	out->id = frame_no; /* vendor 口径:帧号即扩展 ID 0x0001..0x0007 */
	out->dlc = BMS_PROTOCOL_FRAME_DLC;
	out->data[0] = BMS_PROTOCOL_TX_MAGIC;
	out->data[1] = frame_no;

	if (frame_no <= 5U) {
		/* 帧 1-5 按厂家物理 VC1..VC15 槽位排布,未接槽位填 0。 */
		size_t base = (size_t)(frame_no - 1U) * 3U;

		for (size_t i = 0U; i < 3U; ++i) {
			uint8_t vc_channel = (uint8_t)(base + i + 1U);
			uint16_t mv = sample_cell_at_vc_channel(sample, vc_channel);

			put_be16(&out->data[2U + i * 2U], mv);
		}
		return 0;
	}

	if (frame_no == 6U) {
		put_be16(&out->data[2], sample->pack_mv);
		put_be16(&out->data[4], (uint16_t)(energy->soc_0p1pct / 10U));
		put_be16(&out->data[6], (uint16_t)sample->current_ma);
		return 0;
	}

	/* 帧 7:温度(摄氏整数)+ FET 状态 */
	out->data[2] = (uint8_t)(int8_t)(sample->temperature_cdeg[0] / 100);
	out->data[3] = power->dsg_enabled ? 1U : 0U;
	out->data[4] = power->chg_enabled ? 1U : 0U;
	return 0;
}

int bms_protocol_decode_command(const BmsProtocolFrame *in, BmsCommand *out)
{
	if ((in == NULL) || (out == NULL)) {
		return -1;
	}
	if ((in->id != BMS_PROTOCOL_CMD_FRAME_ID) || (in->dlc < 2U) ||
	    (in->data[0] != BMS_PROTOCOL_CMD_MAGIC)) {
		return -1;
	}

	switch (in->data[1]) {
	case BMS_CMD_CLEAR_FAULT:
		out->type = BMS_CMD_CLEAR_FAULT;
		out->arg = 0U;
		return 0;
	case BMS_CMD_ENTER_OTA:
		out->type = BMS_CMD_ENTER_OTA;
		out->arg = 0U;
		return 0;
	default:
		return -1;
	}
}
