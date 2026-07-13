import csv
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]

REQUIRED_BROOKESIA_COMPONENTS = (
    "espressif/brookesia_service_manager",
    "espressif/brookesia_service_helper",
    "espressif/brookesia_hal_interface",
    "espressif/brookesia_service_device",
    "espressif/brookesia_service_audio",
    "espressif/brookesia_agent_manager",
    "espressif/brookesia_agent_xiaozhi",
    "espressif/brookesia_service_nvs",
    "espressif/brookesia_service_sntp",
)


def read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def find_definitions(pattern):
    definitions = []
    for suffix in ("*.c", "*.cc", "*.cpp"):
        for path in (ROOT / "main").rglob(suffix):
            if re.search(pattern, path.read_text(encoding="utf-8")):
                definitions.append(path.relative_to(ROOT).as_posix())
    return sorted(definitions)


def parse_partition_csv(relative_path):
    rows = []
    for row in csv.reader(read(relative_path).splitlines()):
        stripped = [field.strip() for field in row]
        if not stripped or not stripped[0] or stripped[0].startswith("#"):
            continue
        rows.append(stripped)
    return rows


class UnifiedRuntimeFirmwareContractTest(unittest.TestCase):
    def test_shadow_mode_allows_offline_runtime_work_without_passing_hardware(self):
        plan = read(
            "docs/superpowers/plans/2026-07-13-unified-runtime-assistant-merge.md"
        )
        report = read(
            "docs/superpowers/reports/brookesia-xiaozhi-compatibility.md"
        )
        self.assertIn("Tasks 8, 9, 11, and Task 10 preparation", plan)
        self.assertIn("hardware status remains pending", plan)
        self.assertIn("Task 8 may proceed in offline shadow mode", report)
        self.assertIn("single-partition promotion remains gated", report)

    def test_only_launcher_defines_app_main(self):
        definitions = find_definitions(r'extern\s+"C"\s+void\s+app_main\s*\(')
        self.assertEqual(definitions, ["main/main.cpp"])

    def test_assistant_does_not_switch_partitions_or_restart(self):
        source = read("main/apps/app_assistant/app_assistant.cpp")
        self.assertNotIn("esp_ota_set_boot_partition", source)
        self.assertNotIn("esp_restart", source)
        self.assertNotIn('"ota_1"', source)

    def test_partition_table_has_one_factory_application(self):
        rows = parse_partition_csv("partitions.csv")
        app_rows = [row for row in rows if len(row) >= 3 and row[1] == "app"]
        self.assertEqual(len(app_rows), 1)
        self.assertEqual(app_rows[0][2], "factory")

    def test_brookesia_dependencies_are_pinned_to_zero_seven(self):
        manifest = read("main/idf_component.yml")
        self.assertNotIn("brookesia_hal_boards", manifest)
        for component in REQUIRED_BROOKESIA_COMPONENTS:
            version_pattern = re.compile(
                rf"(?m)^\s{{2}}{re.escape(component)}:\s*\n"
                rf"\s+version:\s*['\"]?>=0\.7\.0,<0\.8\.0['\"]?\s*$"
            )
            self.assertRegex(manifest, version_pattern, component)

    def test_standalone_assistant_flash_path_is_removed(self):
        self.assertFalse(
            (ROOT / "scripts" / "flash_assistant_runtime.sh").exists()
        )
        full_flash = read("scripts/flash_full_assistant_stack.sh")
        self.assertNotIn("xiaozhi.bin", full_flash)
        self.assertNotIn("ASSISTANT_APP", full_flash)

    def test_supported_runtime_sources_do_not_depend_on_ota_slot_labels(self):
        offenders = []
        for root in (ROOT / "main", ROOT / "scripts"):
            for path in root.rglob("*"):
                if not path.is_file() or path.suffix not in {
                    ".c",
                    ".cc",
                    ".cpp",
                    ".h",
                    ".hpp",
                    ".py",
                    ".sh",
                }:
                    continue
                source = path.read_text(encoding="utf-8")
                if re.search(r"\bota_[01]\b", source):
                    offenders.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
