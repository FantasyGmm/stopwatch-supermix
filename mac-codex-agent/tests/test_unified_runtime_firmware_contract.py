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

    def test_assistant_uses_runtime_supervisor_and_never_switches_boot_slot(self):
        source = read("main/apps/app_assistant/app_assistant.cpp")
        self.assertIn("GetRuntimeSupervisor().startAssistant()", source)
        self.assertIn("GetRuntimeSupervisor().requestStopAssistant()", source)
        self.assertIn("runtime.update(GetHAL().millis())", source)
        for forbidden in (
            "esp_ota_ops.h",
            "esp_partition.h",
            "esp_system.h",
            "esp_ota_set_boot_partition",
            "esp_restart",
            "ota_1",
        ):
            self.assertNotIn(forbidden, source)

    def test_assistant_mooncake_view_preserves_async_lifecycle_behavior(self):
        source = read("main/apps/app_assistant/app_assistant.cpp")
        header = read("main/apps/app_assistant/app_assistant.h")
        view_header = read("main/apps/app_assistant/view/assistant_view.h")
        view_source = read("main/apps/app_assistant/view/assistant_view.cpp")

        for state in (
            "Connecting",
            "Activating",
            "Listening",
            "Thinking",
            "Speaking",
            "Failed",
            "Closing",
            "StopTimedOut",
        ):
            self.assertIn(state, view_header)
            self.assertIn(f"AssistantViewState::{state}", source + view_source)

        for event_kind in (
            "ActivationCode",
            "UserText",
            "AgentText",
            "Emotion",
        ):
            self.assertIn(f"AssistantEventKind::{event_kind}", source)

        update_at = source.index("runtime.update(GetHAL().millis())")
        drain_at = source.index("drainRuntimeEvents()", update_at)
        key_at = source.index("_key_manager->update()", drain_at)
        self.assertLess(update_at, drain_at)
        self.assertLess(drain_at, key_at)

        self.assertIn("isConversationEvent(event.kind) && _close_requested", source)
        self.assertIn(
            'event.text == "Stopped" && _retry_available && !_close_requested',
            source,
        )
        self.assertRegex(
            source,
            r"_retry_available\s*&&\s*!_close_requested\s*&&\s*"
            r"runtime\.assistantState\(\)\s*==\s*AssistantRuntimeState::Stopped",
        )
        self.assertNotIn("_switch_started", header)
        self.assertNotIn("_switch_at_ms", header)

        for forbidden in (
            "brookesia",
            "runtime/",
            "esp_ota",
            "esp_partition",
            "esp_restart",
        ):
            self.assertNotIn(forbidden, view_header.lower())
            self.assertNotIn(forbidden, view_source.lower())

    def test_unified_candidate_has_one_factory_application(self):
        rows = parse_partition_csv("partitions_unified.csv")
        app_rows = [row for row in rows if len(row) >= 3 and row[1] == "app"]
        self.assertEqual(
            [(row[0], row[2], row[3], row[4]) for row in app_rows],
            [("app", "factory", "0x20000", "0x780000")],
        )

    def test_brookesia_dependencies_are_pinned_to_zero_seven(self):
        manifest = read("main/idf_component.yml")
        self.assertNotIn("brookesia_hal_boards", manifest)
        for component in REQUIRED_BROOKESIA_COMPONENTS:
            version_pattern = re.compile(
                rf"(?m)^\s{{2}}{re.escape(component)}:\s*\n"
                rf"\s+version:\s*['\"]?>=0\.7\.0,<0\.8\.0['\"]?\s*$"
            )
            self.assertRegex(manifest, version_pattern, component)

    def test_legacy_layout_and_recovery_tools_remain_during_shadow_mode(self):
        legacy = read("partitions.csv")
        self.assertIn("ota_0", legacy)
        self.assertIn("ota_1", legacy)
        self.assertTrue(
            (ROOT / "scripts" / "flash_assistant_runtime.sh").is_file()
        )
        self.assertTrue(
            (ROOT / "scripts" / "flash_full_assistant_stack.sh").is_file()
        )

    def test_new_product_path_has_no_ota_slot_dependency(self):
        paths = [
            ROOT / "main" / "apps" / "app_assistant" / "app_assistant.cpp",
            ROOT / "scripts" / "build_unified_candidate.sh",
        ]
        offenders = [
            path.relative_to(ROOT).as_posix()
            for path in paths
            if re.search(r"\bota_[01]\b", path.read_text(encoding="utf-8"))
        ]
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
