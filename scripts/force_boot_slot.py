#!/usr/bin/env python3
import argparse
import binascii
import os
import struct
import subprocess
import sys
import tempfile


OTADATA_OFFSET = "0xd000"
OTADATA_SIZE = 0x2000
OTADATA_SECTOR_SIZE = 0x1000
OTADATA_ENTRY_SIZE = 32
ESP_OTA_IMG_VALID = 0x2


def entry_crc(seq: int) -> int:
    return binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF


def parse_otadata_entry(data: bytes, offset: int) -> int:
    entry = data[offset : offset + OTADATA_ENTRY_SIZE]
    if len(entry) < OTADATA_ENTRY_SIZE:
        return 0

    seq = struct.unpack_from("<I", entry, 0)[0]
    state = struct.unpack_from("<I", entry, 24)[0]
    crc = struct.unpack_from("<I", entry, 28)[0]
    if seq in (0, 0xFFFFFFFF):
        return 0
    if state != ESP_OTA_IMG_VALID:
        return 0
    if crc != entry_crc(seq):
        return 0
    return seq


def current_max_seq(path: str) -> int:
    with open(path, "rb") as f:
        data = f.read(OTADATA_SIZE)
    return max(parse_otadata_entry(data, 0), parse_otadata_entry(data, OTADATA_SECTOR_SIZE))


def next_seq_for_slot(slot: int, max_seq: int, ota_count: int) -> int:
    seq = max(max_seq + 1, 1)
    while (seq - 1) % ota_count != slot:
        seq += 1
    return seq


def make_otadata(slot: int, seq: int) -> bytes:
    if slot not in (0, 1):
        raise ValueError("slot must be 0 or 1")

    data = bytearray([0xFF] * OTADATA_SIZE)
    seq_bytes = struct.pack("<I", seq)
    crc = entry_crc(seq)

    data[0:4] = seq_bytes
    data[24:28] = struct.pack("<I", ESP_OTA_IMG_VALID)
    data[28:32] = struct.pack("<I", crc)

    # Leave the second OTA data sector erased so there is no stale higher seq.
    assert all(b == 0xFF for b in data[OTADATA_SECTOR_SIZE:])
    return bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description="Force ESP-IDF OTA boot slot by writing otadata directly.")
    parser.add_argument("slot", type=int, choices=(0, 1), help="OTA slot to boot: 0 for launcher, 1 for assistant")
    parser.add_argument("-p", "--port", default=os.environ.get("ESPPORT", "/dev/cu.usbmodem1101"))
    parser.add_argument("-b", "--baud", default="460800")
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--ota-count", type=int, default=2, help="Number of OTA app partitions in the table")
    parser.add_argument(
        "--after",
        choices=("hard_reset", "no_reset"),
        default="hard_reset",
        help="esptool action after writing otadata",
    )
    args = parser.parse_args()

    if args.ota_count < 2:
        raise ValueError("ota-count must be at least 2")

    with tempfile.NamedTemporaryFile(prefix="otadata_current_", suffix=".bin", delete=False) as current:
        current_path = current.name

    read_cmd = [
        args.python,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "-p",
        args.port,
        "-b",
        args.baud,
        "--before",
        "default_reset",
        "--after",
        "no_reset",
        "read_flash",
        OTADATA_OFFSET,
        str(OTADATA_SIZE),
        current_path,
    ]

    try:
        subprocess.check_call(read_cmd)
        max_seq = current_max_seq(current_path)
    finally:
        try:
            os.unlink(current_path)
        except OSError:
            pass

    seq = next_seq_for_slot(args.slot, max_seq, args.ota_count)

    with tempfile.NamedTemporaryFile(prefix=f"otadata_slot{args.slot}_", suffix=".bin", delete=False) as tmp:
        tmp.write(make_otadata(args.slot, seq))
        tmp_path = tmp.name

    try:
        cmd = [
            args.python,
            "-m",
            "esptool",
            "--chip",
            "esp32s3",
            "-p",
            args.port,
            "-b",
            args.baud,
            "--before",
            "default_reset",
            "--after",
            args.after,
            "write_flash",
            "--flash_mode",
            "dio",
            "--flash_freq",
            "80m",
            "--flash_size",
            "16MB",
            OTADATA_OFFSET,
            tmp_path,
        ]
        print(f"Forcing boot to ota_{args.slot} using ota_seq={seq} (previous max={max_seq})")
        subprocess.check_call(cmd)
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
