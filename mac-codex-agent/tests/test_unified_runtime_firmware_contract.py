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
    def test_release_capacity_gates_match_single_system_budget(self):
        build_script = read("scripts/build_unified_candidate.sh")
        flash_script = read("scripts/flash_unified_candidate.sh")
        self.assertIn('--max-app-bytes 0x700000', build_script)
        self.assertIn('--max-assets-bytes 0xA0000', build_script)
        self.assertIn('MAX_APP_BYTES=$((0x700000))', flash_script)
        self.assertIn('MAX_ASSETS_BYTES=$((0xA0000))', flash_script)

    def test_only_launcher_defines_app_main(self):
        definitions = find_definitions(r'extern\s+"C"\s+void\s+app_main\s*\(')
        self.assertEqual(definitions, ["main/main.cpp"])

    def test_production_tree_has_no_uninstalled_template_app(self):
        self.assertFalse((ROOT / "main/apps/app_template").exists())
        for relative_path in ("main/main.cpp", "main/apps/apps.h"):
            source = read(relative_path)
            self.assertNotIn("AppTemplate", source)
            self.assertNotIn("app_template", source)

    def test_asset_tree_has_no_unreferenced_badge_edit_icon(self):
        self.assertFalse(
            (ROOT / "main/assets/images/icon_edit_badge.c").exists()
        )
        self.assertNotIn("icon_edit_badge", read("main/assets/assets.h"))

    def test_assistant_does_not_switch_partitions_or_restart(self):
        source = read("main/apps/app_assistant/app_assistant.cpp")
        self.assertNotIn("esp_ota_set_boot_partition", source)
        self.assertNotIn("esp_restart", source)
        self.assertNotIn('"ota_1"', source)

    def test_assistant_uses_runtime_supervisor_and_never_switches_boot_slot(self):
        source = read("main/apps/app_assistant/app_assistant.cpp")
        main = read("main/main.cpp")
        self.assertIn("runtime.startAssistant()", source)
        self.assertIn("runtime.requestStopAssistant()", source)
        self.assertNotIn("runtime.update(GetHAL().millis())", source)
        self.assertIn("GetRuntimeSupervisor().update(GetHAL().millis())", main)
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
        main = read("main/main.cpp")
        header = read("main/apps/app_assistant/app_assistant.h")
        presentation_header = read(
            "main/apps/app_assistant/assistant_presentation_model.h"
        )
        view_header = read("main/apps/app_assistant/view/assistant_view.h")
        view_source = read("main/apps/app_assistant/view/assistant_view.cpp")

        self.assertIn("enum class AssistantVisualState", presentation_header)
        self.assertIn("void setState(AssistantVisualState state)", view_header)
        for state in (
            "Connecting",
            "Activating",
            "WaitingWake",
            "Listening",
            "Thinking",
            "Speaking",
            "Failed",
            "Closing",
            "StopTimedOut",
        ):
            self.assertIn(state, presentation_header)
            self.assertIn(f"AssistantVisualState::{state}", view_source)
        self.assertNotIn("AssistantViewState", source + header + view_header + view_source)

        for event_kind in (
            "ActivationCode",
            "UserText",
            "AgentText",
            "Emotion",
        ):
            self.assertIn(f"AssistantEventKind::{event_kind}", source)

        update_at = main.index("GetRuntimeSupervisor().update(GetHAL().millis())")
        mooncake_at = main.index("GetMooncake().update()", update_at)
        self.assertLess(update_at, mooncake_at)

        drain_at = source.index("drainRuntimeEvents()")
        key_at = source.index("_key_manager->update()", drain_at)
        self.assertLess(drain_at, key_at)

        self.assertIn(
            "isConversationEvent(event.kind) && _close_controller.requested()",
            source,
        )
        self.assertIn(
            'event.text == "Stopped" && _retry_available && !_close_controller.requested()',
            source,
        )
        self.assertRegex(
            source,
            r"_retry_available\s*&&\s*!_close_controller\.requested\(\)\s*&&\s*"
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
        app_rows = [row for row in rows if len(row) >= 3 and row[1] == "app"]
        self.assertEqual([(row[0], row[2]) for row in app_rows], [("app", "factory")])

        previous_end = 0
        for row in rows:
            offset = int(row[3], 0)
            size = int(row[4], 0)
            self.assertGreaterEqual(offset, previous_end, row[0])
            previous_end = offset + size
        self.assertEqual(previous_end, 0xFB0000)
        self.assertEqual(0x1000000 - previous_end, 0x50000)

    def test_brookesia_dependencies_are_pinned_to_zero_seven(self):
        manifest = read("main/idf_component.yml")
        self.assertNotIn("brookesia_hal_boards", manifest)
        for component in REQUIRED_BROOKESIA_COMPONENTS:
            version_pattern = re.compile(
                rf"(?m)^\s{{2}}{re.escape(component)}:\s*\n"
                rf"\s+version:\s*['\"]?>=0\.7\.0,<0\.8\.0['\"]?\s*$"
            )
            self.assertRegex(manifest, version_pattern, component)

    def test_audio_dependency_solver_is_pinned_to_verified_compatible_set(self):
        manifest = read("main/idf_component.yml")
        self.assertIn("espressif/esp_codec_dev: ~1.5.4", manifest)
        self.assertIn("espressif/cmake_utilities: 0.5.3", manifest)
        self.assertIn("jason-mao/av_processor: 0.6.6", manifest)

    def test_promoted_layout_removes_shadow_table_and_legacy_slot_tools(self):
        self.assertFalse((ROOT / "partitions_unified.csv").exists())
        for relative_path in (
            "scripts/flash_assistant_runtime.sh",
            "scripts/flash_full_assistant_stack.sh",
            "scripts/force_boot_slot.py",
        ):
            self.assertFalse((ROOT / relative_path).exists(), relative_path)

        defaults = read("sdkconfig.defaults")
        build_script = read("scripts/build_unified_candidate.sh")
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"', defaults)
        self.assertIn('--partition-table "$ROOT/partitions.csv"', build_script)
        self.assertNotIn("partitions_unified.csv", build_script)

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

    def test_offline_validation_runner_covers_every_automated_gate_without_hardware(self):
        runner = read("scripts/run_unified_offline_validation.sh")

        required_commands = (
            "mac-codex-agent/.venv/bin/python",
            "-m unittest discover -s mac-codex-agent/tests -p 'test_*.py'",
            "tests/assistant_state_machine_test.cpp",
            "tests/assistant_event_mailbox_test.cpp",
            "tests/resource_supervisor_test.cpp",
            "tests/shared_wifi_policy_test.cpp",
            "tests/runtime_diagnostics_test.cpp",
            "./scripts/build_unified_candidate.sh --clean",
            "git diff --check",
        )
        for command in required_commands:
            self.assertIn(command, runner)

        for forbidden in (
            "esptool",
            "read_flash",
            "write_flash",
            "erase_flash",
            "/dev/cu.",
            "/dev/tty",
            "idf.py flash",
            "idf.py monitor",
        ):
            self.assertNotIn(forbidden, runner)

        self.assertNotIn("STOPWATCH_VOICE_KEY_FILE", runner)
        self.assertIn("STOPWATCH_BUILD_DIR", runner)
        self.assertIn("STOPWATCH_SDKCONFIG", runner)
        self.assertNotIn("secrets.token_hex(16)", runner)
        self.assertNotIn("$ROOT/build-unified", runner)

    def test_settings_wifi_page_uses_controller_owned_round_screen_ui(self):
        app_setup = read("main/apps/app_setup/app_setup.cpp")
        worker_header = read("main/apps/app_setup/workers/workers.h")
        worker_path = ROOT / "main/apps/app_setup/workers/wifi.cpp"

        self.assertTrue(worker_path.exists(), worker_path)
        worker = worker_path.read_text(encoding="utf-8")

        self.assertIn('"WiFi"', app_setup)
        self.assertIn("std::make_unique<WifiSettingsWorker>()", app_setup)
        self.assertLess(app_setup.index('"Time & Date"'), app_setup.index('"WiFi"'))
        self.assertLess(app_setup.index('"WiFi"'), app_setup.index('"Firmware"'))

        self.assertIn("class WifiSettingsWorker", worker_header)
        self.assertIn("WifiSettingsController", worker_header)
        self.assertIn("_next_poll_tick", worker_header)

        for required in (
            "WifiSettingsController",
            "WifiSettingsSnapshot",
            "Add WiFi",
            "Stop Pairing",
            "Done",
            "kScreenSize = 466",
            "kListWidth = 350",
            "setSize(kListWidth,",
            "setScrollDir(LV_DIR_VER)",
            "_controller.reset();",
            "_view.reset();",
            "_next_poll_tick = now + 500",
        ):
            self.assertIn(required, worker)

        destructor = worker.index("WifiSettingsWorker::~WifiSettingsWorker")
        release_runtime = worker.index("releaseRuntime();", destructor)
        destroy_view = worker.index("_view.reset();", destructor)
        release_implementation = worker.index("void WifiSettingsWorker::releaseRuntime")
        self.assertLess(release_runtime, destroy_view)
        self.assertIn("_controller.reset();", worker[release_implementation:])
        self.assertNotIn("WifiManager", worker)
        self.assertNotIn("SetEventCallback", worker)

    def test_settings_wifi_page_renders_connection_and_pairing_snapshot_details(self):
        worker = read("main/apps/app_setup/workers/wifi.cpp")

        for required in (
            "snapshot.ssid",
            "snapshot.ip_address",
            "snapshot.rssi",
            '"IP: "',
            '"  RSSI: "',
            '" dBm"',
            "_pairing_ap_ssid_detail",
            "_pairing_portal_url_detail",
            "snapshot.ap_ssid",
            "snapshot.ap_url",
            '"AP SSID: "',
            '"Portal: "',
            "kListTop = 150",
            "kContentWidth",
            "kDefaultSsidLabelWidth",
            "kNonDefaultSsidLabelWidth",
        ):
            self.assertIn(required, worker)

        self.assertNotIn("_pairing_detail", worker)
        self.assertNotIn("LV_LABEL_LONG_MODE_DOTS", worker)


if __name__ == "__main__":
    unittest.main()
