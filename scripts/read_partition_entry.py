#!/usr/bin/env python3
"""Read one validated entry from an ESP-IDF binary partition table."""

import argparse
import dataclasses
import hashlib
import pathlib
import struct
import sys


ENTRY = struct.Struct("<2sBBLL16sL")
ENTRY_MAGIC = b"\xaa\x50"
MD5_PREFIX = b"\xeb\xeb" + b"\xff" * 14
END_MARKER = b"\xff" * ENTRY.size
STOPWATCH_SINGLE_SYSTEM_PROFILE = "stopwatch-single-system-v1"


class PartitionTableError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class PartitionEntry:
    name: str
    type: int
    subtype: int
    offset: int
    size: int
    flags: int


STOPWATCH_SINGLE_SYSTEM_ENTRIES = (
    PartitionEntry("nvs", 1, 2, 0x9000, 0x10000, 0),
    PartitionEntry("phy_init", 1, 1, 0x19000, 0x1000, 0),
    PartitionEntry("app", 0, 0, 0x20000, 0xDC0000, 0),
    PartitionEntry("storage", 1, 0x81, 0xDE0000, 0x100000, 0),
    PartitionEntry("assets", 1, 0x82, 0xEE0000, 0xC0000, 0),
    PartitionEntry("coredump", 1, 3, 0xFA0000, 0x10000, 0),
)


def profile_mismatch(profile, detail):
    raise PartitionTableError(f"profile {profile} mismatch: {detail}")


def format_entry(entry):
    return (
        f"{entry.name}(type=0x{entry.type:x}, subtype=0x{entry.subtype:x}, "
        f"offset=0x{entry.offset:x}, size=0x{entry.size:x}, flags=0x{entry.flags:x})"
    )


def validate_profile(profile, data, entries):
    if profile != STOPWATCH_SINGLE_SYSTEM_PROFILE:
        profile_mismatch(profile, "unknown profile")
    if len(data) != 0xC00:
        profile_mismatch(profile, f"table length 0x{len(data):x} != 0xc00")
    expected = STOPWATCH_SINGLE_SYSTEM_ENTRIES
    if len(entries) != len(expected):
        profile_mismatch(
            profile, f"entry count {len(entries)} != {len(expected)}"
        )
    for index, (actual, required) in enumerate(zip(entries, expected)):
        if actual != required:
            profile_mismatch(
                profile,
                f"entry {index} expected {format_entry(required)} got "
                f"{format_entry(actual)}",
            )


def read_partition_table(path, profile=None):
    data = pathlib.Path(path).read_bytes()
    if profile == STOPWATCH_SINGLE_SYSTEM_PROFILE and len(data) != 0xC00:
        profile_mismatch(profile, f"table length 0x{len(data):x} != 0xc00")
    if len(data) % ENTRY.size:
        raise PartitionTableError("partition table length is not a multiple of 32")
    entries = []
    digest = hashlib.md5()
    md5_seen = False
    for index in range(0, len(data), ENTRY.size):
        chunk = data[index:index + ENTRY.size]
        if chunk == END_MARKER:
            break
        if md5_seen:
            if chunk[:16] == MD5_PREFIX:
                raise PartitionTableError("duplicate partition table MD5 record")
            raise PartitionTableError(
                "partition table MD5 record must be terminal before padding"
            )
        if chunk[:16] == MD5_PREFIX:
            if chunk[16:] != digest.digest():
                raise PartitionTableError("partition table MD5 mismatch")
            md5_seen = True
            for padding_index in range(index + ENTRY.size, len(data), ENTRY.size):
                padding = data[padding_index:padding_index + ENTRY.size]
                if padding == END_MARKER:
                    continue
                if padding[:16] == MD5_PREFIX:
                    raise PartitionTableError("duplicate partition table MD5 record")
                raise PartitionTableError(
                    "partition table MD5 record must be terminal before padding"
                )
            continue
        magic, kind, subtype, offset, size, raw_name, flags = ENTRY.unpack(chunk)
        if magic != ENTRY_MAGIC:
            raise PartitionTableError(f"invalid entry magic at byte 0x{index:x}")
        digest.update(chunk)
        name = raw_name.split(b"\0", 1)[0].decode("utf-8")
        entries.append(PartitionEntry(name, kind, subtype, offset, size, flags))
    if not md5_seen:
        raise PartitionTableError("partition table MD5 record is missing")
    if profile is not None:
        validate_profile(profile, data, entries)
    return entries


def parse_args(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--table", required=True, type=pathlib.Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--field", required=True,
                        choices=("offset", "size", "type", "subtype"))
    parser.add_argument(
        "--profile", choices=(STOPWATCH_SINGLE_SYSTEM_PROFILE,)
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        entry = next(
            (item for item in read_partition_table(args.table, args.profile)
             if item.name == args.name),
            None,
        )
        if entry is None:
            raise PartitionTableError(f"partition not found: {args.name}")
        print(f"0x{getattr(entry, args.field):x}")
        return 0
    except (OSError, UnicodeDecodeError, PartitionTableError) as error:
        print(f"Partition table error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
