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


def pack_entry(kind, subtype, offset, size, name, magic=b"\xaa\x50"):
    return ENTRY.pack(
        magic, kind, subtype, offset, size,
        name.encode().ljust(16, b"\0"), 0,
    )


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
        pack_entry(kind, subtype, offset, size, name)
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
            ["python3", str(SCRIPT), "--table", str(self.table),
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

    def test_rejects_entry_after_terminal_md5(self):
        data = self.table.read_bytes()
        md5_end = ENTRY.size * (ROW_COUNT + 1)
        trailing_entry = pack_entry(1, 4, 0xFB0000, 0x1000, "late")
        self.table.write_bytes(data[:md5_end] + trailing_entry + b"\xff" * (0xC00 - md5_end - ENTRY.size))
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
