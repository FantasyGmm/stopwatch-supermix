import hashlib
import os
import pathlib
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
ENTRY = struct.Struct("<2sBBLL16sL")


def write_test_partition_table(
    path,
    app_size=0xDC0000,
    assets_size=0xC0000,
    app_offset=0x20000,
    assets_offset=0xEE0000,
):
    rows = [
        (1, 2, 0x9000, 0x10000, "nvs"),
        (1, 1, 0x19000, 0x1000, "phy_init"),
        (0, 0, app_offset, app_size, "app"),
        (1, 0x81, 0xDE0000, 0x100000, "storage"),
        (1, 0x82, assets_offset, assets_size, "assets"),
        (1, 3, 0xFA0000, 0x10000, "coredump"),
    ]
    payload = b"".join(
        ENTRY.pack(
            b"\xaa\x50", kind, subtype, offset, size,
            name.encode().ljust(16, b"\0"), 0,
        )
        for kind, subtype, offset, size, name in rows
    )
    payload += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(payload).digest()
    path.write_bytes(payload + b"\xff" * (0xC00 - len(payload)))


def write_reader_aware_python_wrapper(path):
    reader = shlex.quote(str(SCRIPTS / "read_partition_entry.py"))
    real_python = shlex.quote(sys.executable)
    path.write_text(
        "#!/bin/sh\n"
        f"if [ \"$1\" = {reader} ]; then\n"
        f"  exec {real_python} \"$@\"\n"
        "fi\n"
        "printf 'Unexpected test Python invocation: %s\\n' \"$*\" >&2\n"
        "exit 97\n",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def run_script(name, *args):
    environment = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix="reader wrapper ") as temp_dir:
        wrapper = write_reader_aware_python_wrapper(
            pathlib.Path(temp_dir) / "python wrapper"
        )
        environment["PYTHON"] = str(wrapper)
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
        write_test_partition_table(
            self.build / "partition_table" / "partition-table.bin"
        )
        (self.build / "StopWatch-UserDemo.bin").write_bytes(b"app")
        (self.build / "generated_assets.bin").write_bytes(b"assets")

    def tearDown(self):
        self.temp_dir.cleanup()

    def assert_rejected_before_write(self, result):
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("write_flash", result.stdout)

    def test_candidate_flash_policy_never_requires_a_firmware_backup(self):
        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--build-dir",
            self.build,
        )
        source = (SCRIPTS / "flash_unified_candidate.sh").read_text(
            encoding="utf-8"
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("write_flash", result.stdout)
        self.assertNotIn("--backup", source)
        self.assertNotIn("verify_backup", source)

    def test_hardware_campaign_does_not_schedule_backup_recovery(self):
        result = run_script(
            "run_unified_hardware_validation.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--build-dir",
            self.build,
            "--output-dir",
            self.root / "hardware evidence",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("--backup", result.stdout)
        self.assertNotIn("full-flash-recovery", result.stdout)

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
            "--build-dir",
            self.build,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        for offset in ("0x000000", "0x008000", "0x020000", "0xee0000"):
            self.assertEqual(result.stdout.split().count(offset), 1, result.stdout)
        self.assertNotIn("0x8a0000", result.stdout.lower())
        for artifact in (
            "bootloader.bin",
            "partition-table.bin",
            "StopWatch-UserDemo.bin",
            "generated_assets.bin",
        ):
            self.assertIn(artifact, result.stdout)

    def test_candidate_dry_run_rejects_valid_md5_partition_map_drift_before_write(self):
        write_test_partition_table(
            self.build / "partition_table" / "partition-table.bin",
            app_size=0x10000,
            assets_size=0xA0000,
            app_offset=0x30000,
            assets_offset=0xEF0000,
        )

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("stopwatch-single-system-v1", result.stderr)

    def test_reader_aware_python_wrapper_delegates_reader_and_blocks_esptool(self):
        with tempfile.TemporaryDirectory(prefix="reader wrapper ") as temp_dir:
            wrapper = write_reader_aware_python_wrapper(
                pathlib.Path(temp_dir) / "python wrapper"
            )
            reader = subprocess.run(
                [
                    str(wrapper),
                    str(SCRIPTS / "read_partition_entry.py"),
                    "--table",
                    str(self.build / "partition_table" / "partition-table.bin"),
                    "--name",
                    "assets",
                    "--field",
                    "offset",
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            esptool = subprocess.run(
                [str(wrapper), "-m", "esptool"],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=False,
            )

        self.assertEqual(reader.returncode, 0, reader.stderr)
        self.assertEqual(reader.stdout.strip(), "0xee0000")
        self.assertNotEqual(esptool.returncode, 0)
        self.assertIn("Unexpected test Python invocation", esptool.stderr)

    def test_candidate_uses_build_python_when_environment_has_no_python_override(self):
        fake_python = self.root / "idf python"
        reader = shlex.quote(str(SCRIPTS / "read_partition_entry.py"))
        real_python = shlex.quote(sys.executable)
        fake_python.write_text(
            "#!/bin/sh\n"
            f"if [ \"$1\" = {reader} ]; then\n"
            f"  exec {real_python} \"$@\"\n"
            "fi\n"
            "printf 'FAKE_IDF_PYTHON %s\\n' \"$*\"\n",
            encoding="utf-8",
        )
        fake_python.chmod(0o755)
        (self.build / "CMakeCache.txt").write_text(
            f"PYTHON:UNINITIALIZED={fake_python}\n",
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment.pop("PYTHON", None)

        result = subprocess.run(
            [
                "bash",
                str(SCRIPTS / "flash_unified_candidate.sh"),
                "--port",
                "/dev/test",
                "--build-dir",
                str(self.build),
            ],
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("FAKE_IDF_PYTHON -m esptool", result.stdout)

    def test_recovery_exercise_dry_run_restores_then_reinstalls_candidate(self):
        result = run_script(
            "exercise_full_flash_recovery.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--backup",
            self.backup,
            "--build-dir",
            self.build,
            "--output-dir",
            self.root / "recovery evidence",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        restore = result.stdout.index("RECOVERY STEP restore-backup")
        legacy_boot = result.stdout.index("RECOVERY STEP validate-restored-boot")
        candidate = result.stdout.index("RECOVERY STEP flash-unified-candidate")
        unified_boot = result.stdout.index("RECOVERY STEP validate-unified-boot")
        self.assertLess(restore, legacy_boot)
        self.assertLess(legacy_boot, candidate)
        self.assertLess(candidate, unified_boot)
        self.assertIn("write_flash", result.stdout)
        self.assertIn("StopWatch-UserDemo.bin", result.stdout)

    def test_hardware_release_campaign_dry_run_includes_every_gate(self):
        result = run_script(
            "run_unified_hardware_validation.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--build-dir",
            self.build,
            "--output-dir",
            self.root / "hardware evidence",
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        for stage in (
            "flash-candidate",
            "validate-candidate-boot",
            "mixed-10-cycles",
            "product-smoke",
            "mixed-50-cycles",
            "final-product-smoke",
            "secure-multi-agent-acceptance",
        ):
            self.assertIn(f"HARDWARE STEP {stage}", result.stdout)
        self.assertIn("--cycles 10", result.stdout)
        self.assertIn("--cycles 50", result.stdout)
        self.assertGreaterEqual(result.stdout.count("--profiles mixed"), 2)
        ten_cycle_line = next(
            line
            for line in result.stdout.splitlines()
            if "--cycles 10" in line
        )
        self.assertIn("--warmup", ten_cycle_line)
        self.assertIn("secure-pairing-acceptance.md", result.stdout)
        for check in (
            "two-agent-enrollment",
            "non-interrupting-pairing",
            "active-agent-switch",
            "signed-agent-rename",
            "online-rekey",
            "offline-rekey",
            "old-epoch-rejection",
            "eight-agent-limit",
            "reset-all-agents",
            "mandarin-voice-input",
            "assistant-recovery",
            "ble-memory-stability",
        ):
            self.assertIn(check, result.stdout)

    def test_hardware_campaign_propagates_build_python_to_every_flash_step(self):
        fake_python = self.root / "idf python"
        reader = shlex.quote(str(SCRIPTS / "read_partition_entry.py"))
        real_python = shlex.quote(sys.executable)
        fake_python.write_text(
            "#!/bin/sh\n"
            f"if [ \"$1\" = {reader} ]; then\n"
            f"  exec {real_python} \"$@\"\n"
            "fi\n"
            "exit 0\n",
            encoding="utf-8",
        )
        fake_python.chmod(0o755)
        (self.build / "CMakeCache.txt").write_text(
            f"PYTHON:UNINITIALIZED={fake_python}\n",
            encoding="utf-8",
        )
        environment = os.environ.copy()
        environment.pop("PYTHON", None)

        result = subprocess.run(
            [
                "bash",
                str(SCRIPTS / "run_unified_hardware_validation.sh"),
                "--dry-run",
                "--port",
                "/dev/test",
                "--build-dir",
                str(self.build),
                "--output-dir",
                str(self.root / "hardware evidence"),
            ],
            cwd=ROOT,
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        write_lines = [line for line in result.stdout.splitlines() if "write_flash" in line]
        self.assertEqual(len(write_lines), 1, result.stdout)
        self.assertTrue(
            all(str(fake_python).replace(" ", "\\ ") in line for line in write_lines),
            result.stdout,
        )

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

    def test_candidate_rejects_app_over_capacity_before_printing_write(self):
        with (self.build / "StopWatch-UserDemo.bin").open("wb") as app_file:
            app_file.truncate(0x660000 + 1)

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Unified application exceeds capacity limit", result.stderr)

    def test_candidate_rejects_assets_over_capacity_before_printing_write(self):
        with (self.build / "generated_assets.bin").open("wb") as assets_file:
            assets_file.truncate(0xA0000 + 1)

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Unified assets exceeds capacity limit", result.stderr)

    def test_candidate_keeps_app_generated_partition_capacity_guard(self):
        script = (SCRIPTS / "flash_unified_candidate.sh").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'check_max_size "$APP" "$APP_PARTITION_BYTES" '
            '"Unified application exceeds partition capacity"',
            script,
        )

    def test_candidate_keeps_assets_generated_partition_capacity_guard(self):
        script = (SCRIPTS / "flash_unified_candidate.sh").read_text(
            encoding="utf-8"
        )

        self.assertIn(
            'check_max_size "$ASSETS" "$ASSETS_PARTITION_BYTES" '
            '"Unified assets exceeds partition capacity"',
            script,
        )

    def test_candidate_uses_strict_profile_for_each_partition_lookup(self):
        script = (SCRIPTS / "flash_unified_candidate.sh").read_text(
            encoding="utf-8"
        )

        self.assertIn('PARTITION_PROFILE="stopwatch-single-system-v1"', script)
        self.assertEqual(script.count('--profile "$PARTITION_PROFILE"'), 4)

    def test_candidate_rejects_invalid_partition_table_before_printing_write(self):
        (self.build / "partition_table" / "partition-table.bin").write_bytes(b"bad")

        result = run_script(
            "flash_unified_candidate.sh",
            "--dry-run",
            "--port",
            "/dev/test",
            "--build-dir",
            self.build,
        )

        self.assert_rejected_before_write(result)
        self.assertIn("Partition table error", result.stderr)

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
