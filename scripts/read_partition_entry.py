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


class PartitionTableError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class PartitionEntry:
    name: str
    type: int
    subtype: int
    offset: int
    size: int


def read_partition_table(path):
    data = pathlib.Path(path).read_bytes()
    if len(data) % ENTRY.size:
        raise PartitionTableError("partition table length is not a multiple of 32")
    entries = []
    digest = hashlib.md5()
    md5_seen = False
    for index in range(0, len(data), ENTRY.size):
        chunk = data[index:index + ENTRY.size]
        if chunk == END_MARKER:
            break
        if chunk[:16] == MD5_PREFIX:
            if chunk[16:] != digest.digest():
                raise PartitionTableError("partition table MD5 mismatch")
            md5_seen = True
            continue
        magic, kind, subtype, offset, size, raw_name, _flags = ENTRY.unpack(chunk)
        if magic != ENTRY_MAGIC:
            raise PartitionTableError(f"invalid entry magic at byte 0x{index:x}")
        digest.update(chunk)
        name = raw_name.split(b"\0", 1)[0].decode("utf-8")
        entries.append(PartitionEntry(name, kind, subtype, offset, size))
    if not md5_seen:
        raise PartitionTableError("partition table MD5 record is missing")
    return entries


def parse_args(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--table", required=True, type=pathlib.Path)
    parser.add_argument("--name", required=True)
    parser.add_argument("--field", required=True,
                        choices=("offset", "size", "type", "subtype"))
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        entry = next(
            (item for item in read_partition_table(args.table)
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
