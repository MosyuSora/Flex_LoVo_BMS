#include "algo/bms_diag_format.h"

/* 轻量字符串拼接:诊断路径不引入 printf 家族,省 ROM 也免重入问题 */
static char *append_char(char *out, const char *end, char value)
{
	if (out < end) {
		*out++ = value;
	}
	return out;
}

static char *append_str(char *out, const char *end, const char *text)
{
	while ((text != NULL) && (*text != '\0')) {
		out = append_char(out, end, *text++);
	}
	return out;
}

static char *append_u32(char *out, const char *end, uint32_t value)
{
	char digits[10];
	size_t count = 0U;

	do {
		digits[count++] = (char)('0' + (value % 10U));
		value /= 10U;
	} while ((value != 0U) && (count < sizeof(digits)));

	while (count > 0U) {
		out = append_char(out, end, digits[--count]);
	}
	return out;
}

static char *append_i32(char *out, const char *end, int32_t value)
{
	if (value < 0) {
		out = append_char(out, end, '-');
		value = -value;
	}
	return append_u32(out, end, (uint32_t)value);
}

static char *append_hex8(char *out, const char *end, uint8_t value)
{
	static const char digits[] = "0123456789ABCDEF";

	out = append_char(out, end, digits[(value >> 4) & 0x0FU]);
	out = append_char(out, end, digits[value & 0x0FU]);
	return out;
}

size_t bms_diag_format_snapshot(char *buf, size_t buf_size, const BmsSnapshotView *view)
{
	char *out = buf;
	const char *end;

	if ((buf == NULL) || (buf_size == 0U) || (view == NULL) ||
	    (view->sample == NULL)) {
		return 0U;
	}
	end = buf + buf_size;

	out = append_str(out, end, "SNAP version=");
	out = append_u32(out, end, view->sample->version);
	out = append_str(out, end, " pack_mv=");
	out = append_u32(out, end, view->sample->pack_mv);
	out = append_str(out, end, " cells_mv=[");
	for (size_t i = 0U; i < BMS_SAMPLE_CELL_COUNT; ++i) {
		if (i != 0U) {
			out = append_char(out, end, ',');
		}
		out = append_u32(out, end, view->sample->cell_mv[i]);
	}
	out = append_str(out, end, "] current_ma=");
	out = append_i32(out, end, view->sample->current_ma);
	out = append_str(out, end, " temp_cdeg=[");
	for (size_t i = 0U; i < BMS_SAMPLE_TEMP_COUNT; ++i) {
		if (i != 0U) {
			out = append_char(out, end, ',');
		}
		out = append_i32(out, end, view->sample->temperature_cdeg[i]);
	}
	out = append_str(out, end, "] temp_valid=");
	out = append_u32(out, end, view->sample->temp_valid);
	out = append_str(out, end, " flags=");
	out = append_u32(out, end, view->sample->flags);

	if (view->energy != NULL) {
		out = append_str(out, end, " soc_0p1=");
		out = append_u32(out, end, view->energy->soc_0p1pct);
		out = append_str(out, end, " soh_0p1=");
		out = append_u32(out, end, view->energy->soh_0p1pct);
		out = append_str(out, end, " conf_0p1=");
		out = append_u32(out, end, view->energy->confidence_0p1pct);
		out = append_str(out, end, " sop_chg_ma=");
		out = append_u32(out, end, view->energy->charge_limit_ma);
		out = append_str(out, end, " sop_dsg_ma=");
		out = append_u32(out, end, view->energy->discharge_limit_ma);
	}
	if (view->fault != NULL) {
		out = append_str(out, end, " fault_bits=");
		out = append_u32(out, end, view->fault->fault_bits);
		out = append_str(out, end, " afe_status=");
		out = append_u32(out, end, view->fault->afe_status);
	}
	if (view->balance != NULL) {
		out = append_str(out, end, " bal_req=");
		out = append_u32(out, end, view->balance->requested_mask);
		out = append_str(out, end, " bal_mask=");
		out = append_u32(out, end, view->balance->applied_mask);
		out = append_str(out, end, " bal_min_mv=");
		out = append_u32(out, end, view->balance->min_cell_mv);
		out = append_str(out, end, " bal_max_mv=");
		out = append_u32(out, end, view->balance->max_cell_mv);
		out = append_str(out, end, " bal_delta_mv=");
		out = append_u32(out, end, view->balance->delta_mv);
	}
	if (view->power != NULL) {
		out = append_str(out, end, " pwr_mode=");
		out = append_u32(out, end, (uint32_t)view->power->mode);
		out = append_str(out, end, " chg=");
		out = append_u32(out, end, view->power->chg_enabled ? 1U : 0U);
		out = append_str(out, end, " dsg=");
		out = append_u32(out, end, view->power->dsg_enabled ? 1U : 0U);
	}
	if (view->selftest != NULL) {
		out = append_str(out, end, " hb_stale=");
		out = append_u32(out, end, view->selftest->heartbeat_stale_mask);
	}
	out = append_str(out, end, "\r\n");

	return (size_t)(out - buf);
}

size_t bms_diag_format_afe_reg(char *buf, size_t buf_size, uint8_t addr, int rc, uint8_t value)
{
	char *out = buf;
	const char *end;

	if ((buf == NULL) || (buf_size == 0U)) {
		return 0U;
	}
	end = buf + buf_size;

	if (rc == 0) {
		out = append_str(out, end, "AFE_REG addr=");
		out = append_hex8(out, end, addr);
		out = append_str(out, end, " value=");
		out = append_hex8(out, end, value);
		out = append_str(out, end, "\r\n");
	} else {
		out = append_str(out, end, "AFE_REG_ERR addr=");
		out = append_hex8(out, end, addr);
		out = append_str(out, end, " rc=");
		out = append_i32(out, end, rc);
		out = append_str(out, end, "\r\n");
	}
	return (size_t)(out - buf);
}
