"""Parser and presentation helpers for the Zephyr UART SNAP protocol."""

from __future__ import annotations

import re
from typing import Any


TOKEN_RE = re.compile(r"([A-Za-z][A-Za-z0-9_]*)=((?:\[[^\]]*\])|(?:[^\s]+))")

ACTIVE_CELL_CHANNELS = (1, 2, 5, 6, 7, 10, 11, 12, 15)

FAULT_NAMES = {
    0: "单体过压",
    1: "单体欠压",
    2: "放电过流",
    3: "充电过流",
    4: "充电过温",
    5: "充电低温",
    6: "放电过温",
    7: "放电低温",
    8: "AFE 告警",
    9: "AFE 通信",
    10: "采样无效",
    11: "NTC 故障",
}

POWER_NAMES = {
    0: "空闲",
    1: "充电",
    2: "放电",
    3: "故障",
}

AFE_REGISTER_NAMES = {
    0x00: "SYS_STAT",
    0x01: "CELLBAL1",
    0x02: "CELLBAL2",
    0x03: "CELLBAL3",
    0x04: "SYS_CTRL1",
    0x05: "SYS_CTRL2",
    0x06: "PROTECT1",
    0x07: "PROTECT2",
    0x08: "PROTECT3",
    0x09: "OV_TRIP",
    0x0A: "UV_TRIP",
    0x0B: "CC_CFG",
    0x2A: "BAT_HI",
    0x2B: "BAT_LO",
    0x2C: "TS1_HI",
    0x2D: "TS1_LO",
    0x2E: "TS2_HI",
    0x2F: "TS2_LO",
    0x30: "TS3_HI",
    0x31: "TS3_LO",
    0x32: "CC_HI",
    0x33: "CC_LO",
    0x50: "ADCGAIN1",
    0x51: "ADCOFFSET",
    0x59: "ADCGAIN2",
}
for _cell in range(1, 16):
    AFE_REGISTER_NAMES[0x0C + (_cell - 1) * 2] = f"VC{_cell}_HI"
    AFE_REGISTER_NAMES[0x0D + (_cell - 1) * 2] = f"VC{_cell}_LO"

AFE_REG_RE = re.compile(
    r"\bAFE_REG\s+addr=([0-9A-Fa-f]{2})\s+value=([0-9A-Fa-f]{2})\b"
)


def _parse_value(raw: str) -> Any:
    if raw.startswith("[") and raw.endswith("]"):
        body = raw[1:-1].strip()
        if not body:
            return []
        return [_parse_value(part.strip()) for part in body.split(",")]
    try:
        return int(raw, 10)
    except ValueError:
        return raw


def parse_snap_line(line: str) -> dict[str, Any] | None:
    """Parse one complete SNAP line; return None for unrelated UART text."""
    match = re.search(r"\bSNAP\s+(.+?)\s*$", line.strip())
    if not match:
        return None

    fields: dict[str, Any] = {}
    for key, raw in TOKEN_RE.findall(match.group(1)):
        fields[key] = _parse_value(raw)

    if "version" not in fields or "cells_mv" not in fields:
        return None
    fields["fault_names"] = fault_names(int(fields.get("fault_bits", 0)))
    fields["power_name"] = POWER_NAMES.get(int(fields.get("pwr_mode", 0)), "未知")
    fields["temp_c"] = [round(int(value) / 100.0, 2) for value in fields.get("temp_cdeg", [])]
    fields["soc_percent"] = round(int(fields.get("soc_0p1", 0)) / 10.0, 1)
    fields["soh_percent"] = round(int(fields.get("soh_0p1", 0)) / 10.0, 1)
    fields["pack_v"] = round(int(fields.get("pack_mv", 0)) / 1000.0, 3)
    fields["current_a"] = round(int(fields.get("current_ma", 0)) / 1000.0, 3)
    return fields


def parse_afe_reg_line(line: str) -> tuple[int, int] | None:
    """Parse one raw AFE register diagnostic line."""
    match = AFE_REG_RE.search(line.strip())
    if not match:
        return None
    return int(match.group(1), 16), int(match.group(2), 16)


def fault_names(bits: int) -> list[str]:
    return [name for bit, name in FAULT_NAMES.items() if bits & (1 << bit)]


def cell_map(values: list[Any]) -> dict[int, int | None]:
    """Map the 15-slot shipping protocol to VC channel numbers."""
    result: dict[int, int | None] = {}
    for channel in range(1, 16):
        index = ACTIVE_CELL_CHANNELS.index(channel) if channel in ACTIVE_CELL_CHANNELS else None
        if index is None or index >= len(values):
            result[channel] = None
            continue
        try:
            result[channel] = int(values[index])
        except (TypeError, ValueError):
            result[channel] = None
    return result
