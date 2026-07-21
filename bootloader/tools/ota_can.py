#!/usr/bin/env python3
"""
CAN OTA 上位机:让运行中的 App 进 OTA,再把新 App 镜像分页传给 Bootloader。

依赖:python-can(pip install python-can)。CAN 适配器按你的硬件配 --interface/--channel。

流程(与 Bootloader bl_ota.c / App fault_protection enter_ota 对齐):
  1. (可选)发 ENTER_OTA 命令帧(0x0100, [0xA5,0x02]):App 关双 FET → 置 BKP → 复位进 BL。
  2. 发 OTA_START(0x0700):length(4,BE)+crc32(4,BE);等 STATUS READY。
  3. 分页发 DATA(0x0701,每 8B);每 1KB 等一个 PAGE_OK。
  4. 发 OTA_END(0x0702):version(4,BE);等 DONE(成功→BL 写头复位跳新 App)或 CRC_FAIL。

用法:
  python ota_can.py <app.bin> --version 2 [--enter-ota] \
      --interface socketcan --channel can0        # Linux SocketCAN
      # 或 --interface pcan --channel PCAN_USBBUS1 等,按你的适配器
"""
import argparse
import struct
import sys
import time

import can  # python-can

from header_gen import stm32_hw_crc32

CMD_FRAME_ID = 0x0100
CMD_MAGIC = 0xA5
CMD_ENTER_OTA = 0x02

OTA_START = 0x0700
OTA_DATA = 0x0701
OTA_END = 0x0702
OTA_STATUS = 0x0708

ST_READY, ST_PAGE_OK, ST_PAGE_ERR, ST_DONE, ST_CRC_FAIL, ST_ERR = 0, 1, 2, 3, 4, 5
ST_NAME = {0: "READY", 1: "PAGE_OK", 2: "PAGE_ERR", 3: "DONE", 4: "CRC_FAIL", 5: "ERR"}
PAGE = 1024


def send(bus, arb_id, data):
    bus.send(can.Message(arbitration_id=arb_id, is_extended_id=True, data=bytes(data)))


def wait_status(bus, want, timeout=5.0):
    """等一条 STATUS 帧,返回 (code, page)。"""
    end = time.time() + timeout
    while time.time() < end:
        m = bus.recv(timeout=end - time.time())
        if m is None:
            break
        if m.is_extended_id and m.arbitration_id == OTA_STATUS and len(m.data) >= 1:
            code = m.data[0]
            page = (m.data[1] << 8) | m.data[2] if len(m.data) >= 3 else 0
            if want is None or code == want:
                return code, page
            if code in (ST_PAGE_ERR, ST_CRC_FAIL, ST_ERR):
                return code, page  # 出错也立刻返回
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app_bin")
    ap.add_argument("--version", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--enter-ota", action="store_true", help="先发命令让 App 进 OTA")
    ap.add_argument("--interface", default="socketcan")
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--bitrate", type=int, default=500000)
    args = ap.parse_args()

    app = open(args.app_bin, "rb").read()
    pad = (-len(app)) % 4
    app += b"\xff" * pad
    length = len(app)
    crc = stm32_hw_crc32(app)
    print(f"app={args.app_bin} len={length} crc32=0x{crc:08X} version={args.version}")

    bus = can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)

    if args.enter_ota:
        print("发 ENTER_OTA,等 App 关 FET + 复位进 Bootloader ...")
        send(bus, CMD_FRAME_ID, [CMD_MAGIC, CMD_ENTER_OTA, 0, 0, 0, 0, 0, 0])
        time.sleep(1.0)  # 给 App 关 FET(回读重试)+ 复位 + BL 起来的时间

    print("START ...")
    send(bus, OTA_START, struct.pack(">II", length, crc))
    code, _ = wait_status(bus, ST_READY, timeout=8.0)  # 擦区可能 ~1-2s
    if code != ST_READY:
        print(f"START 失败:{ST_NAME.get(code, code)}")
        sys.exit(1)

    total_pages = (length + PAGE - 1) // PAGE
    off = 0
    for pg in range(total_pages):
        chunk = app[off:off + PAGE]
        for i in range(0, len(chunk), 8):
            frame = chunk[i:i + 8]
            send(bus, OTA_DATA, frame + b"\x00" * (8 - len(frame)))
        code, page = wait_status(bus, None, timeout=5.0)
        if code != ST_PAGE_OK:
            print(f"页 {pg} 失败:{ST_NAME.get(code, code)}")
            sys.exit(1)
        off += len(chunk)
        print(f"\r页 {pg + 1}/{total_pages} OK", end="", flush=True)
    print()

    print("END ...")
    send(bus, OTA_END, struct.pack(">I", args.version) + b"\x00" * 4)
    code, _ = wait_status(bus, None, timeout=8.0)
    if code == ST_DONE:
        print("OTA 成功:Bootloader 已写头并复位,新 App 启动。")
    else:
        print(f"OTA 失败:{ST_NAME.get(code, code)}(镜像未提交,包保持离线安全态,可重传)")
        sys.exit(1)


if __name__ == "__main__":
    main()
