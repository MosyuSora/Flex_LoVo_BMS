#!/usr/bin/env python3
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


FRAME_RE = re.compile(r"BMS_CAN_(?:FRAME|TX)\s+(\{.*\})")
ACTIVE_CELL_CHANNELS = {1, 2, 5, 6, 7, 10, 11, 12, 15}


def repo_root():
    return Path(__file__).resolve().parents[2]


def run_renode(root):
    script = root / "tests" / "renode" / "run_zephyr_uart_smoke.ps1"
    command = [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(script),
        "-Scenario",
        "normal",
        "-CanInitWorkaround",
    ]
    return subprocess.run(
        command,
        cwd=str(root),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def read_uart_log(root):
    log_path = root / "tests" / "renode" / "artifacts" / "zephyr_uart_normal.log"
    try:
        return log_path.read_text(encoding="ascii", errors="replace")
    except OSError:
        return ""


def parse_frames(output):
    frames = []
    for line in output.splitlines():
        match = FRAME_RE.search(line)
        if not match:
            continue

        try:
            frame = json.loads(match.group(1))
            if frame.get("result", 0) != 0:
                continue

            frame_id = frame["id"]
            if isinstance(frame_id, str):
                frame_id = int(frame_id, 0)
            frame["id"] = "0x%04X" % frame_id
            frame["data_bytes"] = [int(part, 16) for part in frame["data"].split()]
        except (json.JSONDecodeError, KeyError, TypeError, ValueError):
            # Zephyr printk output from multiple threads can occasionally interleave.
            continue

        frames.append(frame)
    return frames


def u16(data, index):
    return (data[index] << 8) | data[index + 1]


def i16(data, index):
    value = u16(data, index)
    return value - 0x10000 if value & 0x8000 else value


def decode_frames(frames):
    latest = {}
    for frame in frames:
        latest[frame["id"]] = frame

    decoded = {
        "cells_mv": [],
        "pack_mv": None,
        "soc_percent": None,
        "current_ma": None,
        "temperature_c": None,
        "dsg_state_raw": None,
        "chg_state_raw": None,
        "frames": latest,
    }

    for frame_id in ["0x0001", "0x0002", "0x0003", "0x0004", "0x0005"]:
        frame = latest.get(frame_id)
        if not frame:
            continue
        data = frame["data_bytes"]
        decoded["cells_mv"].extend([u16(data, 2), u16(data, 4), u16(data, 6)])

    frame6 = latest.get("0x0006")
    if frame6:
        data = frame6["data_bytes"]
        decoded["pack_mv"] = u16(data, 2)
        decoded["soc_percent"] = u16(data, 4)
        decoded["current_ma"] = i16(data, 6)

    frame7 = latest.get("0x0007")
    if frame7:
        data = frame7["data_bytes"]
        decoded["temperature_c"] = data[2]
        decoded["dsg_state_raw"] = data[3]
        decoded["chg_state_raw"] = data[4]

    return decoded


def validate(decoded):
    errors = []
    expected_ids = ["0x%04X" % i for i in range(1, 8)]
    present_ids = set(decoded["frames"].keys())
    missing = [frame_id for frame_id in expected_ids if frame_id not in present_ids]
    if missing:
        errors.append("missing CAN frames: " + ", ".join(missing))

    cells = decoded["cells_mv"]
    if len(cells) != 15:
        errors.append("expected 15 BQ VC slot voltages, got %d" % len(cells))
    for index, value in enumerate(cells, start=1):
        if index in ACTIVE_CELL_CHANNELS:
            if value < 2000 or value > 5000:
                errors.append("cell%d voltage out of expected range: %dmV" % (index, value))
        elif value != 0:
            errors.append("inactive cell%d expected 0mV, got %dmV" % (index, value))

    if decoded["pack_mv"] is None:
        errors.append("missing pack voltage")
    if decoded["temperature_c"] is None:
        errors.append("missing temperature")
    return errors


def print_summary(decoded, errors):
    print("BMS Virtual Upper Computer")
    print("frames: %d" % len(decoded["frames"]))

    if decoded["cells_mv"]:
        cells = " ".join("%d:%dmV" % (i, v) for i, v in enumerate(decoded["cells_mv"], start=1))
        print("cells: " + cells)

    print("pack_mv: %s" % decoded["pack_mv"])
    print("soc_percent: %s" % decoded["soc_percent"])
    print("current_ma: %s" % decoded["current_ma"])
    print("temperature_c: %s" % decoded["temperature_c"])
    print("dsg_state_raw: %s" % decoded["dsg_state_raw"])
    print("chg_state_raw: %s" % decoded["chg_state_raw"])
    print("status: %s" % ("PASS" if not errors else "FAIL"))
    for error in errors:
        print("error: " + error)


def main():
    parser = argparse.ArgumentParser(description="Run the BMS Renode test bench and decode virtual upper-computer CAN frames.")
    parser.add_argument(
        "--report",
        default="tests/reports/virtual_upper_latest.json",
    )
    parser.add_argument("--show-renode-output", action="store_true")
    args = parser.parse_args()

    root = repo_root()
    result = run_renode(root)
    if args.show_renode_output:
        print(result.stdout)

    frames = parse_frames(result.stdout + "\n" + read_uart_log(root))
    decoded = decode_frames(frames)
    errors = validate(decoded)
    if result.returncode != 0:
        errors.append("Renode exited with code %d" % result.returncode)

    report_path = root / args.report
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "frames": [
            {"id": frame["id"], "dlc": frame["dlc"], "data": frame["data"]}
            for frame in frames
        ],
        "decoded": {
            key: value
            for key, value in decoded.items()
            if key != "frames"
        },
        "errors": errors,
    }
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print_summary(decoded, errors)
    print("report: %s" % report_path)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
