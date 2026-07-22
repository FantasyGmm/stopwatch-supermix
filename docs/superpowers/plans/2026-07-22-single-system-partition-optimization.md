# Single-System Flash Partition Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the 7.5 MiB App / 4 MiB Assets layout with the approved 13.75 MiB App / 1 MiB Storage / 768 KiB Assets layout while retaining USB recovery and preventing flash-address drift.

**Architecture:** Keep one `factory` application and continue locating Storage and Assets by label at runtime. Build the new table from `partitions.csv`, read App and Assets addresses and sizes from the generated binary table before flashing, retain the current App regression budget, and reduce the Assets budget to 640 KiB. The first hardware migration retains a verified full-Flash backup and initializes the relocated Storage partition as a clean filesystem.

**Tech Stack:** ESP-IDF 5.5.4, ESP32-S3 partition tables, Bash, Python 3 standard library, `unittest`, existing unified build and hardware-validation scripts.

---

## File Map

| File | Responsibility |
|---|---|
| `partitions.csv` | Authoritative 16 MiB layout |
| `scripts/read_partition_entry.py` | Validate/query the generated binary partition table |
| `scripts/flash_unified_candidate.sh` | Backup, capacity, and flash-address guards |
| `scripts/build_unified_candidate.sh` | Release capacity gates and metrics |
| `mac-codex-agent/tests/test_partition_table_reader.py` | Binary reader unit tests |
| `mac-codex-agent/tests/test_unified_flash_tools.py` | Flash-script contracts with a real-format test table |
| `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py` | Product partition and budget contracts |
| `README.md` | Operator-facing layout and migration documentation |

Do not reset, stage, or commit unrelated dirty-worktree changes. Every commit below stages only named files.

### Task 1: Replace the partition layout under a failing contract

**Files:**
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`
- Modify: `partitions.csv`

- [ ] **Step 1: Write the complete layout contract**

Replace the existing default-partition test with:

```python
def test_default_partition_table_has_approved_single_system_layout(self):
    rows = parse_partition_csv("partitions.csv")
    self.assertEqual(
        [(row[0], row[1], row[2], row[3], row[4]) for row in rows],
        [
            ("nvs", "data", "nvs", "0x9000", "0x10000"),
            ("phy_init", "data", "phy", "0x19000", "0x1000"),
            ("app", "app", "factory", "0x20000", "0xDC0000"),
            ("storage", "data", "fat", "0xDE0000", "0x100000"),
            ("assets", "data", "spiffs", "0xEE0000", "0xC0000"),
            ("coredump", "data", "coredump", "0xFA0000", "0x10000"),
        ],
    )
    app_rows = [row for row in rows if row[1] == "app"]
    self.assertEqual([(row[0], row[2]) for row in app_rows], [("app", "factory")])

    previous_end = 0
    for row in rows:
        offset = int(row[3], 0)
        size = int(row[4], 0)
        self.assertGreaterEqual(offset, previous_end, row[0])
        previous_end = offset + size
    self.assertEqual(previous_end, 0xFB0000)
    self.assertEqual(0x1000000 - previous_end, 0x50000)
```

- [ ] **Step 2: Run the contract and confirm it fails on the old table**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_unified_runtime_firmware_contract.py'
```

Expected: FAIL showing old App/Assets values.

- [ ] **Step 3: Install the approved `partitions.csv`**

```csv
# Name,   Type, SubType, Offset,    Size,      Flags
nvs,      data, nvs,     0x9000,    0x10000,
phy_init, data, phy,     0x19000,   0x1000,
app,      app,  factory, 0x20000,   0xDC0000,
storage,  data, fat,     0xDE0000,  0x100000,
assets,   data, spiffs,  0xEE0000,  0xC0000,
coredump, data, coredump,0xFA0000,  0x10000,
```

- [ ] **Step 4: Run Step 2 again**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add partitions.csv mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
git commit -m "Optimize single-system flash partition layout"
```

### Task 2: Add a generated partition-table reader

**Files:**
- Create: `scripts/read_partition_entry.py`
- Create: `mac-codex-agent/tests/test_partition_table_reader.py`

- [ ] **Step 1: Write the reader tests**

Create `mac-codex-agent/tests/test_partition_table_reader.py`:

```python
import hashlib
import pathlib
import struct
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "read_partition_entry.py"
ENTRY = struct.Struct("<2sBBLL16sL")
MD5_PREFIX = b"\xeb\xeb" + b"\xff" * 14
PROFILE = "stopwatch-single-system-v1"

def write_table(path, corrupt=False):
    rows = [
        (1, 2, 0x9000, 0x10000, "nvs"),
        (1, 1, 0x19000, 0x1000, "phy_init"),
        (0, 0, 0x20000, 0xDC0000, "app"),
        (1, 0x81, 0xDE0000, 0x100000, "storage"),
        (1, 0x82, 0xEE0000, 0xC0000, "assets"),
        (1, 3, 0xFA0000, 0x10000, "coredump"),
    ]
    payload = b"".join(
        ENTRY.pack(
            b"\xaa\x50", kind, subtype, offset, size,
            name.encode().ljust(16, b"\0"), 0,
        )
        for kind, subtype, offset, size, name in rows
    )
    digest = bytearray(hashlib.md5(payload).digest())
    if corrupt:
        digest[0] ^= 0xFF
    payload += MD5_PREFIX + bytes(digest)
    path.write_bytes(payload + b"\xff" * (0xC00 - len(payload)))

class PartitionTableReaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.table = pathlib.Path(self.temp.name) / "partition-table.bin"
        write_table(self.table)

    def tearDown(self):
        self.temp.cleanup()

    def run_reader(self, name, field):
        return subprocess.run(
            ["python3", str(SCRIPT), "--profile", PROFILE,
             "--table", str(self.table),
             "--name", name, "--field", field],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )

    def test_reads_offset_and_size(self):
        offset = self.run_reader("assets", "offset")
        size = self.run_reader("assets", "size")
        self.assertEqual(offset.returncode, 0, offset.stderr)
        self.assertEqual(size.returncode, 0, size.stderr)
        self.assertEqual(offset.stdout.strip(), "0xee0000")
        self.assertEqual(size.stdout.strip(), "0xc0000")

    def test_rejects_missing_partition(self):
        result = self.run_reader("missing", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("partition not found: missing", result.stderr)

    def test_rejects_corrupt_md5(self):
        write_table(self.table, corrupt=True)
        result = self.run_reader("assets", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("partition table MD5 mismatch", result.stderr)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and confirm the missing-script failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_partition_table_reader.py'
```

Expected: FAIL because the reader is absent.

- [ ] **Step 3: Implement the hardened profile reader**

Create `scripts/read_partition_entry.py`:

The earlier generic-reader draft is superseded: the flash path must use only
`stopwatch-single-system-v1`. It must reject every generated table except an
exact 0xC00-byte table with these six canonical, NUL-padded raw labels in this
order (all flags `0`): `nvs` `(type=1, subtype=2, offset=0x9000,
size=0x10000)`, `phy_init` `(1, 1, 0x19000, 0x1000)`, `app` `(0, 0,
0x20000, 0xDC0000)`, `storage` `(1, 0x81, 0xDE0000, 0x100000)`, `assets`
`(1, 0x82, 0xEE0000, 0xC0000)`, and `coredump` `(1, 3, 0xFA0000,
0x10000)`. The MD5 record must be terminal and all remaining 32-byte records
must be `0xFF` padding.

```python
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
    raw_name: bytes

def canonical_raw_name(name):
    return name.encode("ascii").ljust(16, b"\0")

def expected_entry(name, kind, subtype, offset, size):
    return PartitionEntry(name, kind, subtype, offset, size, 0,
                          canonical_raw_name(name))

STOPWATCH_SINGLE_SYSTEM_ENTRIES = (
    expected_entry("nvs", 1, 2, 0x9000, 0x10000),
    expected_entry("phy_init", 1, 1, 0x19000, 0x1000),
    expected_entry("app", 0, 0, 0x20000, 0xDC0000),
    expected_entry("storage", 1, 0x81, 0xDE0000, 0x100000),
    expected_entry("assets", 1, 0x82, 0xEE0000, 0xC0000),
    expected_entry("coredump", 1, 3, 0xFA0000, 0x10000),
)

def profile_mismatch(profile, detail):
    raise PartitionTableError(f"profile {profile} mismatch: {detail}")

def format_entry(entry):
    return (
        f"{entry.name}(type=0x{entry.type:x}, subtype=0x{entry.subtype:x}, "
        f"offset=0x{entry.offset:x}, size=0x{entry.size:x}, flags=0x{entry.flags:x}, "
        f"raw_name={entry.raw_name.hex()})"
    )

def validate_profile(profile, data, entries):
    if profile != STOPWATCH_SINGLE_SYSTEM_PROFILE:
        profile_mismatch(profile, "unknown profile")
    if len(data) != 0xC00:
        profile_mismatch(profile, f"table length 0x{len(data):x} != 0xc00")
    expected = STOPWATCH_SINGLE_SYSTEM_ENTRIES
    if len(entries) != len(expected):
        profile_mismatch(profile, f"entry count {len(entries)} != {len(expected)}")
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
                "partition table MD5 record must be terminal before padding")
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
                    "partition table MD5 record must be terminal before padding")
            continue
        magic, kind, subtype, offset, size, raw_name, flags = ENTRY.unpack(chunk)
        if magic != ENTRY_MAGIC:
            raise PartitionTableError(f"invalid entry magic at byte 0x{index:x}")
        digest.update(chunk)
        name = raw_name.split(b"\0", 1)[0].decode("utf-8")
        entries.append(PartitionEntry(name, kind, subtype, offset, size, flags,
                                      raw_name))
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
    parser.add_argument("--profile", choices=(STOPWATCH_SINGLE_SYSTEM_PROFILE,))
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
```

- [ ] **Step 4: Run Step 2 again**

Expected: 3 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/read_partition_entry.py mac-codex-agent/tests/test_partition_table_reader.py
git commit -m "Read generated flash partition entries safely"
```

### Task 3: Make flashing follow the generated table

**Files:**
- Modify: `mac-codex-agent/tests/test_unified_flash_tools.py`
- Modify: `scripts/flash_unified_candidate.sh`

- [ ] **Step 1: Make the flash fixture generate a valid table**

Add `import struct`, then add:

```python
ENTRY = struct.Struct("<2sBBLL16sL")

def write_test_partition_table(path, app_size=0xDC0000, assets_size=0xC0000):
    rows = [
        (1, 2, 0x9000, 0x10000, "nvs"),
        (1, 1, 0x19000, 0x1000, "phy_init"),
        (0, 0, 0x20000, app_size, "app"),
        (1, 0x81, 0xDE0000, 0x100000, "storage"),
        (1, 0x82, 0xEE0000, assets_size, "assets"),
        (1, 3, 0xFA0000, 0x10000, "coredump"),
    ]
    payload = b"".join(
        ENTRY.pack(b"\xaa\x50", kind, subtype, offset, size,
                   name.encode().ljust(16, b"\0"), 0)
        for kind, subtype, offset, size, name in rows
    )
    payload += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(payload).digest()
    path.write_bytes(payload + b"\xff" * (0xC00 - len(payload)))
```

Replace the `b"partitions"` fixture with:

```python
write_test_partition_table(self.build / "partition_table" / "partition-table.bin")
```

- [ ] **Step 2: Update and extend flash contracts**

Change expected Assets offset from `0x8a0000` to `0xee0000` and the Assets release-overflow fixture from `0x400000 + 1` to `0xA0000 + 1`.

Add:

```python
def test_candidate_rejects_app_larger_than_generated_partition(self):
    write_test_partition_table(
        self.build / "partition_table" / "partition-table.bin", app_size=3
    )
    result = run_script(
        "flash_unified_candidate.sh", "--dry-run", "--port", "/dev/test",
        "--backup", self.backup, "--build-dir", self.build,
    )
    self.assert_rejected_before_write(result)
    self.assertIn("Unified application exceeds partition capacity", result.stderr)

def test_candidate_rejects_assets_larger_than_generated_partition(self):
    write_test_partition_table(
        self.build / "partition_table" / "partition-table.bin", assets_size=3
    )
    result = run_script(
        "flash_unified_candidate.sh", "--dry-run", "--port", "/dev/test",
        "--backup", self.backup, "--build-dir", self.build,
    )
    self.assert_rejected_before_write(result)
    self.assertIn("Unified assets exceeds partition capacity", result.stderr)
```

- [ ] **Step 3: Run the suite and confirm failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_unified_flash_tools.py'
```

Expected: FAIL on old offset/budget and absent partition-size guards.

- [ ] **Step 4: Implement dynamic offsets and partition-size guards**

In `scripts/flash_unified_candidate.sh`, set:

```bash
MAX_ASSETS_BYTES=$((0xA0000))
```

Replace `check_max_size` with:

```bash
check_max_size() {
    local path="$1"
    local max_size="$2"
    local label="$3"
    local size
    size="$(file_size "$path")"
    if (( size > max_size )); then
        echo "$label: $size > $max_size bytes" >&2
        return 1
    fi
}
```

After requiring all build artifacts, add:

```bash
PARTITION_READER="$ROOT/scripts/read_partition_entry.py"
PARTITION_PROFILE="stopwatch-single-system-v1"
require_file "$PARTITION_READER"
APP_OFFSET="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name app --field offset)"
APP_PARTITION_BYTES="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name app --field size)"
ASSETS_OFFSET="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name assets --field offset)"
ASSETS_PARTITION_BYTES="$("$PYTHON_BIN" "$PARTITION_READER" --profile "$PARTITION_PROFILE" --table "$PARTITION_TABLE" --name assets --field size)"

check_max_size "$APP" "$MAX_APP_BYTES" "Unified application exceeds capacity limit"
check_max_size "$APP" "$APP_PARTITION_BYTES" "Unified application exceeds partition capacity"
check_max_size "$ASSETS" "$MAX_ASSETS_BYTES" "Unified assets exceeds capacity limit"
check_max_size "$ASSETS" "$ASSETS_PARTITION_BYTES" "Unified assets exceeds partition capacity"
```

Remove the old duplicate checks. In the esptool command replace fixed App/Assets offsets with:

```bash
"$APP_OFFSET" "$APP"
"$ASSETS_OFFSET" "$ASSETS"
```

- [ ] **Step 5: Run reader and flash tests**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_partition_table_reader.py'
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_unified_flash_tools.py'
```

Expected: both PASS; dry-run contains `0x020000` and `0xee0000` once.

- [ ] **Step 6: Commit**

```bash
git add scripts/flash_unified_candidate.sh mac-codex-agent/tests/test_unified_flash_tools.py
git commit -m "Flash unified assets from generated layout"
```

### Task 4: Synchronize build and release budgets

**Files:**
- Modify: `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`
- Modify: `scripts/build_unified_candidate.sh`

- [ ] **Step 1: Add the budget contract**

```python
def test_release_capacity_gates_match_single_system_budget(self):
    build_script = read("scripts/build_unified_candidate.sh")
    flash_script = read("scripts/flash_unified_candidate.sh")
    self.assertIn('--max-app-bytes 0x660000', build_script)
    self.assertIn('--max-assets-bytes 0xA0000', build_script)
    self.assertIn('MAX_APP_BYTES=$((0x660000))', flash_script)
    self.assertIn('MAX_ASSETS_BYTES=$((0xA0000))', flash_script)
```

- [ ] **Step 2: Run the runtime contract and confirm failure**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_unified_runtime_firmware_contract.py'
```

Expected: FAIL because the build script still uses the 4 MiB Assets budget.

- [ ] **Step 3: Change only the Assets build gate**

In `scripts/build_unified_candidate.sh` use:

```bash
--max-assets-bytes 0xA0000
```

Keep `--max-app-bytes 0x660000`; the larger partition must not silently relax binary regression control.

- [ ] **Step 4: Run focused suites**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_unified_runtime_firmware_contract.py'
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_report_firmware_metrics.py'
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_unified_flash_tools.py'
```

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add scripts/build_unified_candidate.sh mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
git commit -m "Set unified asset release budget"
```

### Task 5: Update operator documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace active layout documentation**

Document exactly:

```markdown
- Flash layout: one 13.75 MiB factory application, 1 MiB FAT storage,
  768 KiB read-only Assistant assets, and a 64 KiB coredump partition on
  16 MiB Flash

The default `partitions.csv` contains one 13.75 MiB factory application at
`0x20000`, 1 MiB writable FAT storage at `0xDE0000`, and 768 KiB of generated
Assistant assets at `0xEE0000`. The release remains USB-only and has no OTA
application slot or automatic rollback. Before the first write of this layout,
retain a checksum-verified complete 16 MiB Flash backup.

The first migration initializes the relocated Storage partition as a clean FAT
filesystem. Existing Storage files are not copied automatically. NVS keeps its
starting offset, but settings preservation must be verified after migration.
```

- [ ] **Step 2: Scan for stale active values**

```bash
rg -n "7\.5 MiB|4 MiB|0x8a0000|0x780000|0x400000" \
  README.md partitions.csv scripts/build_unified_candidate.sh \
  scripts/flash_unified_candidate.sh
```

Expected: no stale active-layout matches.

- [ ] **Step 3: Verify and commit**

```bash
git diff --check -- README.md
git add README.md
git commit -m "Document optimized unified flash layout"
```

### Task 6: Run clean offline verification

**Files:**
- Verify only; do not stage generated build files

- [ ] **Step 1: Run every Python contract**

```bash
mac-codex-agent/.venv/bin/python -m unittest discover \
  -s mac-codex-agent/tests -p 'test_*.py'
```

Expected: all PASS.

- [ ] **Step 2: Run the full offline gate**

```bash
source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh
./scripts/run_unified_offline_validation.sh
```

Expected: host C++ tests, Python tests, dependency checks, clean unified build, capacity gates, and whitespace checks PASS.

- [ ] **Step 3: Query the generated layout**

```bash
python3 scripts/read_partition_entry.py --profile stopwatch-single-system-v1 --table build-unified/partition_table/partition-table.bin --name app --field offset
python3 scripts/read_partition_entry.py --profile stopwatch-single-system-v1 --table build-unified/partition_table/partition-table.bin --name app --field size
python3 scripts/read_partition_entry.py --profile stopwatch-single-system-v1 --table build-unified/partition_table/partition-table.bin --name assets --field offset
python3 scripts/read_partition_entry.py --profile stopwatch-single-system-v1 --table build-unified/partition_table/partition-table.bin --name assets --field size
```

Expected, in order:

```text
0x20000
0xdc0000
0xee0000
0xc0000
```

- [ ] **Step 4: Inspect metrics and source status**

```bash
sed -n '1,220p' build-unified/unified-runtime-metrics.json
git status --short
git diff --check
```

Expected: `app_partition_bytes` is `14417920`, App is at most `6684672`, Assets is at most `655360`, and `capacity_gate_passed` is `true`. No generated lock/config drift may be staged or committed.

### Task 7: Perform the guarded hardware migration

**Files:**
- Hardware operation only; backup artifacts remain outside Git

- [ ] **Step 1: Detect the live port**

```bash
STOPWATCH_PORTS="$(ls -1 /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null || true)"
STOPWATCH_PORT_COUNT="$(printf '%s\n' "$STOPWATCH_PORTS" | sed '/^$/d' | wc -l | tr -d ' ')"
test "$STOPWATCH_PORT_COUNT" -eq 1 || {
  printf 'Expected one StopWatch port, found %s:\n%s\n' \
    "$STOPWATCH_PORT_COUNT" "$STOPWATCH_PORTS" >&2
  exit 1
}
STOPWATCH_PORT="$STOPWATCH_PORTS"
printf 'Using %s\n' "$STOPWATCH_PORT"
```

Expected: identify the connected StopWatch port. Stop if none is present.

- [ ] **Step 2: Create a verified full backup**

```bash
./scripts/backup_watch_flash.sh \
  --port "$STOPWATCH_PORT" \
  --output backups/watch-before-single-system-layout.bin
wc -c backups/watch-before-single-system-layout.bin
shasum -a 256 backups/watch-before-single-system-layout.bin
```

Expected: exactly `16777216` bytes and a SHA256 matching the sidecar.

- [ ] **Step 3: Check the write map without writing**

```bash
./scripts/flash_unified_candidate.sh \
  --dry-run --port "$STOPWATCH_PORT" \
  --backup backups/watch-before-single-system-layout.bin \
  --build-dir build-unified
```

Expected: writes at `0x000000`, `0x008000`, `0x020000`, and `0xEE0000`; no erase and no Storage write.

- [ ] **Step 4: Flash**

```bash
./scripts/flash_unified_candidate.sh \
  --port "$STOPWATCH_PORT" \
  --backup backups/watch-before-single-system-layout.bin \
  --build-dir build-unified
```

Expected: all writes verify and the watch resets. New Storage formats cleanly.

- [ ] **Step 5: Run product smoke**

Verify Launcher; all retained apps; brightness/volume persistence; Wi-Fi; Codex status; Assistant open/capture/playback/close; clean Storage handling; and absence of OTA/boot-slot behavior.

- [ ] **Step 6: Prove recovery**

```bash
./scripts/exercise_full_flash_recovery.sh \
  --port "$STOPWATCH_PORT" \
  --backup backups/watch-before-single-system-layout.bin \
  --build-dir build-unified \
  --output-dir hardware-evidence/partition-migration-recovery
```

Expected: backup restores and boots, then candidate reinstalls and boots.

- [ ] **Step 7: Record evidence**

Report source commits, App/Assets sizes and hashes, generated addresses, backup size/hash, serial port, flash verification, smoke results, recovery result, and remaining user changes. Do not claim completion if hardware migration or recovery remains unverified.
