#!/usr/bin/env python3
"""
生成 App 头(magic + version + length + STM32 硬件 CRC32),写在 flash 0x08001000。

用法:
  python header_gen.py <app.bin> <version> [out_header.bin]

- app.bin:Zephyr 生成的 App 原始镜像(zephyr.bin),烧在 0x08001400。
- CRC 口径必须与 STM32 片上 CRC 单元一致(init 0xFFFFFFFF, poly 0x04C11DB7,
  按小端 32 位字喂入,不反转、无末异或)——Bootloader/host 三方要一致。
- 镜像 4 字节对齐,不足补 0xFF(与擦净 flash 一致)。
"""
import struct
import sys

APP_HDR_MAGIC = 0x4F544201  # "OTA\x01",须与 Bootloader bl.h 一致


def stm32_hw_crc32(data: bytes) -> int:
    """STM32 片上 CRC 单元口径(按小端 32 位字)。data 必须 4 字节对齐。"""
    crc = 0xFFFFFFFF
    for i in range(0, len(data), 4):
        word = int.from_bytes(data[i:i + 4], "little")
        crc ^= word
        for _ in range(32):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if (crc & 0x80000000) \
                else (crc << 1) & 0xFFFFFFFF
    return crc


def build_header(app_bin: bytes, version: int):
    pad = (-len(app_bin)) % 4
    padded = app_bin + b"\xff" * pad
    length = len(padded)
    crc = stm32_hw_crc32(padded)
    header = struct.pack("<IIII", APP_HDR_MAGIC, version, length, crc)
    return header, length, crc


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    app_path, version = sys.argv[1], int(sys.argv[2], 0)
    out = sys.argv[3] if len(sys.argv) > 3 else "app_header.bin"
    with open(app_path, "rb") as f:
        app = f.read()
    header, length, crc = build_header(app, version)
    with open(out, "wb") as f:
        f.write(header)
    print(f"app={app_path} len={len(app)} padded_len={length} version={version}")
    print(f"crc32=0x{crc:08X}  -> header written to {out} (flash @0x08001000)")


if __name__ == "__main__":
    main()
