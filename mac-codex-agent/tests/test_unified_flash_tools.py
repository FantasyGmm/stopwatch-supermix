import hashlib
import os
import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"


def run_script(name, *args):
    environment = os.environ.copy()
    environment["PYTHON"] = "/definitely/not/an/executable"
    return subprocess.run(
        ["bash", str(SCRIPTS / name), *map(str, args)],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )


class UnifiedFlashToolsTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp_dir.name) / "artifacts with spaces"
        self.root.mkdir()
        self.backup = self.root / "watch backup.bin"
        self.backup.write_bytes(b"\0" * 0x1000000)
        digest = hashlib.sha256(self.backup.read_bytes()).hexdigest()
        pathlib.Path(f"{self.backup}.sha256").write_text(
            f"{digest}  {self.backup}\n",
            encoding="utf-8",
        )
        self.build = self.root / "unified build"
        (self.build / "bootloader").mkdir(parents=True)
        (self.build / "partition_table").mkdir()
        (self.build / "bootloader" / "bootloader.bin").write_bytes(b"boot")
        (self.build / "partition_table" / "partition-table.bin").write_bytes(
            b"partitions"
        )
        (self.build / "StopWatch-UserDemo.bin").write_bytes(b"app")
        (self.build / "generated_assets.bin").write_bytes(b"assets")

    def tearDown(self):
        self.temp_dir.cleanup()

    def assert_rejected_before_write(self, result):
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("write_flash", result.stdout)

    def test_backup_dry_run_reads_the_complete_flash_without_erasing(self):
        output = self.root / "new backup with spaces.bin"
        result = run_script(
            "backup_watch_flash.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--output",
            output,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("read_flash 0x0 0x1000000", result.stdout)
        self.assertIn("new\\ backup\\ with\\ spaces.bin", result.stdout)
        self.assertNotIn("erase_flash", result.stdout)
        self.assertFalse(output.exists())
        self.assertFalse(pathlib.Path(f"{output}.sha256").exists())

    def test_backup_real_path_requires_exact_size_before_writing_sha256(self):
        source = (SCRIPTS / "backup_watch_flash.sh").read_text(encoding="utf-8")

        self.assertNotIn("erase_flash", source)
        size_check = source.index('if [[ "$size" != "$FLASH_BYTES" ]]')
        sidecar_write = source.index(
            'shasum -a 256 "$OUTPUT" > "$OUTPUT.sha256"'
        )
        self.assertLess(size_check, sidecar_write)

    def test_restore_dry_run_validates_backup_then_prints_whole_flash_write(self):
        result = run_script(
            "restore_watch_flash.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("write_flash", result.stdout)
        self.assertIn("0x0", result.stdout)
        self.assertIn("watch\\ backup.bin", result.stdout)

    def test_candidate_dry_run_prints_only_the_planned_four_file_map(self):
        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
            "--build-dir",
            self.build,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        for offset in ("0x000000", "0x008000", "0x020000", "0x8a0000"):
            self.assertEqual(result.stdout.split().count(offset), 1, result.stdout)
        for artifact in (
            "bootloader.bin",
            "partition-table.bin",
            "StopWatch-UserDemo.bin",
            "generated_assets.bin",
        ):
            self.assertIn(artifact, result.stdout)

    def test_restore_rejects_short_backup_before_printing_write(self):
        with self.backup.open("r+b") as backup_file:
            backup_file.truncate(0x1000000 - 1)

        result = run_script(
            "restore_watch_flash.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Backup size is invalid", result.stderr)

    def test_restore_rejects_checksum_mismatch_before_printing_write(self):
        pathlib.Path(f"{self.backup}.sha256").write_text(
            f"{'f' * 64}  {self.backup}\n",
            encoding="utf-8",
        )

        result = run_script(
            "restore_watch_flash.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Backup checksum mismatch", result.stderr)

    def test_candidate_requires_verified_complete_backup_even_in_dry_run(self):
        pathlib.Path(f"{self.backup}.sha256").unlink()

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("checksum sidecar is missing", result.stderr)

    def test_candidate_rejects_short_backup_even_in_dry_run(self):
        with self.backup.open("r+b") as backup_file:
            backup_file.truncate(0x1000000 - 1)

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Backup size is invalid", result.stderr)

    def test_candidate_rejects_mismatched_backup_hash_even_in_dry_run(self):
        pathlib.Path(f"{self.backup}.sha256").write_text(
            f"{'f' * 64}  {self.backup}\n",
            encoding="utf-8",
        )

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Backup checksum mismatch", result.stderr)

    def test_candidate_rejects_app_over_capacity_before_printing_write(self):
        with (self.build / "StopWatch-UserDemo.bin").open("wb") as app_file:
            app_file.truncate(0x660000 + 1)

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Unified application exceeds capacity limit", result.stderr)

    def test_candidate_rejects_assets_over_capacity_before_printing_write(self):
        with (self.build / "generated_assets.bin").open("wb") as assets_file:
            assets_file.truncate(0x400000 + 1)

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Unified assets exceeds capacity limit", result.stderr)

    def test_candidate_requires_each_exact_build_artifact(self):
        artifacts = (
            self.build / "bootloader" / "bootloader.bin",
            self.build / "partition_table" / "partition-table.bin",
            self.build / "StopWatch-UserDemo.bin",
            self.build / "generated_assets.bin",
        )
        for artifact in artifacts:
            with self.subTest(artifact=artifact.name):
                contents = artifact.read_bytes()
                artifact.unlink()
                result = run_script(
                    "flash_unified_candidate.sh",
                    "--dry-run",
                    "--port",
                    "/dev/test",
                    "--backup",
                    self.backup,
                    "--build-dir",
                    self.build,
                )
                self.assert_rejected_before_write(result)
                self.assertIn("Required artifact is missing", result.stderr)
                artifact.parent.mkdir(parents=True, exist_ok=True)
                artifact.write_bytes(contents)

    def test_tools_have_no_erase_or_boot_slot_operation(self):
        for name in (
            "backup_watch_flash.sh",
            "restore_watch_flash.sh",
            "flash_unified_candidate.sh",
        ):
            with self.subTest(script=name):
                source = (SCRIPTS / name).read_text(encoding="utf-8")
                self.assertNotIn("erase_flash", source)
                self.assertNotIn("force_boot_slot", source)
                self.assertNotIn("otadata", source.lower())


if __name__ == "__main__":
    unittest.main()
