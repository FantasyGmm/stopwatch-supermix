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
ROW_COUNT = 6
PROFILE = "stopwatch-single-system-v1"
PROFILE_MISMATCH = f"profile {PROFILE} mismatch"


def pack_entry(kind, subtype, offset, size, name, flags=0, magic=b"\xaa\x50"):
    return ENTRY.pack(
        magic, kind, subtype, offset, size,
        name.encode().ljust(16, b"\0"), flags,
    )


def approved_rows():
    return [
        (1, 2, 0x9000, 0x10000, "nvs"),
        (1, 1, 0x19000, 0x1000, "phy_init"),
        (0, 0, 0x20000, 0xDC0000, "app"),
        (1, 0x81, 0xDE0000, 0x100000, "storage"),
        (1, 0x82, 0xEE0000, 0xC0000, "assets"),
        (1, 3, 0xFA0000, 0x10000, "coredump"),
    ]


def write_table(path, rows=None, corrupt=False, table_length=0xC00):
    rows = approved_rows() if rows is None else rows
    payload = b"".join(
        pack_entry(*row)
        for row in rows
    )
    digest = bytearray(hashlib.md5(payload).digest())
    if corrupt:
        digest[0] ^= 0xFF
    payload += MD5_PREFIX + bytes(digest)
    path.write_bytes(payload + b"\xff" * (table_length - len(payload)))


class PartitionTableReaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.table = pathlib.Path(self.temp.name) / "partition-table.bin"
        write_table(self.table)

    def tearDown(self):
        self.temp.cleanup()

    def run_reader(self, name, field, profile=None):
        command = [
            "python3", str(SCRIPT), "--table", str(self.table),
            "--name", name, "--field", field,
        ]
        if profile is not None:
            command.extend(("--profile", profile))
        return subprocess.run(
            command,
            cwd=ROOT, capture_output=True, text=True, check=False,
        )

    def test_reads_offset_and_size(self):
        offset = self.run_reader("assets", "offset")
        size = self.run_reader("assets", "size")
        self.assertEqual(offset.returncode, 0, offset.stderr)
        self.assertEqual(size.returncode, 0, size.stderr)
        self.assertEqual(offset.stdout.strip(), "0xee0000")
        self.assertEqual(size.stdout.strip(), "0xc0000")

    def test_profile_accepts_exact_approved_generated_layout(self):
        result = self.run_reader("assets", "offset", profile=PROFILE)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "0xee0000")

    def test_profile_rejects_noncanonical_table_length(self):
        write_table(self.table, table_length=0xBE0)

        result = self.run_reader("assets", "offset", profile=PROFILE)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(PROFILE_MISMATCH, result.stderr)
        self.assertIn("length", result.stderr)

    def test_profile_rejects_valid_md5_app_or_assets_offset_drift(self):
        for row_index, changed_offset in ((2, 0x30000), (4, 0xEF0000)):
            with self.subTest(row_index=row_index):
                rows = approved_rows()
                kind, subtype, _offset, size, name = rows[row_index]
                rows[row_index] = (kind, subtype, changed_offset, size, name)
                write_table(self.table, rows=rows)

                result = self.run_reader("assets", "offset", profile=PROFILE)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(PROFILE_MISMATCH, result.stderr)
                self.assertIn("entry", result.stderr)

    def test_profile_rejects_noncanonical_raw_label_after_nul(self):
        data = bytearray(self.table.read_bytes())
        app_entry_start = ENTRY.size * 2
        raw_label_start = app_entry_start + 12
        data[raw_label_start:raw_label_start + 16] = b"app\0NONCANONICAL"
        md5_start = ENTRY.size * ROW_COUNT
        data[md5_start + 16:md5_start + ENTRY.size] = hashlib.md5(
            data[:md5_start]
        ).digest()
        self.table.write_bytes(data)

        generic_result = self.run_reader("app", "offset")
        profile_result = self.run_reader("app", "offset", profile=PROFILE)

        self.assertEqual(generic_result.returncode, 0, generic_result.stderr)
        self.assertEqual(generic_result.stdout.strip(), "0x20000")
        self.assertNotEqual(profile_result.returncode, 0)
        self.assertIn(PROFILE_MISMATCH, profile_result.stderr)
        self.assertIn("entry", profile_result.stderr)

    def test_profile_rejects_wrong_type_subtype_or_flags(self):
        for row_index, replacement in (
            (2, (1, 0, 0x20000, 0xDC0000, "app")),
            (4, (1, 0x81, 0xEE0000, 0xC0000, "assets")),
            (4, (1, 0x82, 0xEE0000, 0xC0000, "assets", 1)),
        ):
            with self.subTest(row_index=row_index, replacement=replacement):
                rows = approved_rows()
                rows[row_index] = replacement
                write_table(self.table, rows=rows)

                result = self.run_reader("assets", "offset", profile=PROFILE)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(PROFILE_MISMATCH, result.stderr)
                self.assertIn("entry", result.stderr)

    def test_profile_rejects_duplicate_or_extra_ota_entry(self):
        for extra_entry in (
            approved_rows()[4],
            (0, 0x10, 0xFB0000, 0x1000, "ota_0"),
        ):
            with self.subTest(extra_entry=extra_entry):
                write_table(self.table, rows=approved_rows() + [extra_entry])

                result = self.run_reader("assets", "offset", profile=PROFILE)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn(PROFILE_MISMATCH, result.stderr)
                self.assertIn("entry count", result.stderr)

    def test_generic_reader_allows_valid_alternate_map_without_profile(self):
        rows = approved_rows()
        rows[4] = (1, 0x82, 0xEF0000, 0xA0000, "assets")
        write_table(self.table, rows=rows)

        result = self.run_reader("assets", "offset")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "0xef0000")

    def test_rejects_missing_partition(self):
        result = self.run_reader("missing", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("partition not found: missing", result.stderr)

    def test_rejects_corrupt_md5(self):
        write_table(self.table, corrupt=True)
        result = self.run_reader("assets", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("partition table MD5 mismatch", result.stderr)

    def test_rejects_entry_after_terminal_md5(self):
        data = self.table.read_bytes()
        md5_end = ENTRY.size * (ROW_COUNT + 1)
        trailing_entry = pack_entry(1, 4, 0xFB0000, 0x1000, "late")
        self.table.write_bytes(data[:md5_end] + trailing_entry + b"\xff" * (0xC00 - md5_end - ENTRY.size))
        result = self.run_reader("late", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MD5 record must be terminal", result.stderr)

    def test_rejects_entry_after_terminal_md5_ff_padding(self):
        data = self.table.read_bytes()
        md5_end = ENTRY.size * (ROW_COUNT + 1)
        trailing_entry = pack_entry(1, 4, 0xFB0000, 0x1000, "late")
        self.table.write_bytes(
            data[:md5_end] + b"\xff" * ENTRY.size + trailing_entry
            + b"\xff" * (0xC00 - md5_end - (2 * ENTRY.size))
        )
        result = self.run_reader("late", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MD5 record must be terminal", result.stderr)

    def test_rejects_duplicate_md5(self):
        data = self.table.read_bytes()
        md5_start = ENTRY.size * ROW_COUNT
        md5_end = md5_start + ENTRY.size
        duplicate = data[md5_start:md5_end]
        self.table.write_bytes(data[:md5_end] + duplicate + b"\xff" * (0xC00 - md5_end - ENTRY.size))
        result = self.run_reader("assets", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate partition table MD5 record", result.stderr)

    def test_rejects_missing_md5(self):
        self.table.write_bytes(self.table.read_bytes()[:ENTRY.size * ROW_COUNT])
        result = self.run_reader("assets", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("partition table MD5 record is missing", result.stderr)

    def test_rejects_invalid_entry_magic(self):
        data = bytearray(self.table.read_bytes())
        data[0] = 0
        self.table.write_bytes(data)
        result = self.run_reader("nvs", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid entry magic", result.stderr)

    def test_reads_type_and_subtype(self):
        partition_type = self.run_reader("nvs", "type")
        subtype = self.run_reader("nvs", "subtype")
        self.assertEqual(partition_type.returncode, 0, partition_type.stderr)
        self.assertEqual(subtype.returncode, 0, subtype.stderr)
        self.assertEqual(partition_type.stdout.strip(), "0x1")
        self.assertEqual(subtype.stdout.strip(), "0x2")

    def test_rejects_malformed_length(self):
        self.table.write_bytes(b"\0")
        result = self.run_reader("assets", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("length is not a multiple of 32", result.stderr)

    def test_rejects_invalid_utf8_name(self):
        data = bytearray(self.table.read_bytes())
        data[12] = 0xFF
        self.table.write_bytes(data)
        result = self.run_reader("nvs", "offset")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid start byte", result.stderr)


if __name__ == "__main__":
    unittest.main()
