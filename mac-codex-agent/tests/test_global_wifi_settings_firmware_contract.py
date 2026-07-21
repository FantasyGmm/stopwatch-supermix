import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def method_body(source, signature_pattern, next_signature_pattern):
    start = re.search(signature_pattern, source)
    if start is None:
        raise AssertionError(f"missing method: {signature_pattern}")
    end = re.search(next_signature_pattern, source[start.end() :])
    if end is None:
        raise AssertionError(f"missing following method: {next_signature_pattern}")
    return source[start.end() : start.end() + end.start()]


class GlobalWifiSettingsFirmwareContractTests(unittest.TestCase):
    def test_settings_model_controller_and_ui_never_expose_passwords_or_callbacks(self):
        settings_sources = {
            "model": read("main/apps/app_setup/workers/wifi_settings_model.h"),
            "controller": read("main/apps/app_setup/workers/wifi_settings_controller.cpp"),
            "ui": read("main/apps/app_setup/workers/wifi.cpp"),
        }

        for name, source in settings_sources.items():
            self.assertNotRegex(source, re.compile(r"\bpassword\b", re.IGNORECASE), name)
            self.assertNotIn("SetEventCallback", source, name)

    def test_settings_controller_owns_shared_wifi_with_settings_owner(self):
        controller = read("main/apps/app_setup/workers/wifi_settings_controller.cpp")

        self.assertIn("GetSharedWifiService()", controller)
        self.assertIn("RuntimeOwner::Settings", controller)
        self.assertIn("SsidManager::GetInstance()", controller)
        self.assertIn("_ssid_manager.GetSsidList()", controller)
        self.assertIn("_ssid_manager.SetDefaultSsid", controller)

    def test_settings_releases_sta_before_config_ap_and_config_ap_before_sta_reacquisition(self):
        controller = read("main/apps/app_setup/workers/wifi_settings_controller.cpp")
        start_pairing = method_body(
            controller,
            r"bool\s+WifiSettingsController::startPairing\s*\(\s*\)",
            r"void\s+WifiSettingsController::stopPairing\s*\(\s*\)",
        )
        stop_pairing = method_body(
            controller,
            r"void\s+WifiSettingsController::stopPairing\s*\(\s*\)",
            r"bool\s+WifiSettingsController::selectSaved\s*\(",
        )
        finish_pairing = re.split(
            r"void\s+WifiSettingsController::finishPairing\s*\(\s*\)", controller, maxsplit=1
        )[1]

        self.assertLess(start_pairing.index("_backend->releaseSta()"), start_pairing.index("_backend->acquireConfigAp()"))
        self.assertLess(stop_pairing.index("_backend->releaseConfigAp()"), stop_pairing.index("finishPairing()"))
        self.assertIn("_backend->releaseConfigAp();", controller)
        self.assertIn("acquireStaWhenSaved();", finish_pairing)

    def test_worker_tears_down_controller_before_view(self):
        worker = read("main/apps/app_setup/workers/wifi.cpp")
        teardown = method_body(
            worker,
            r"WifiSettingsWorker::~WifiSettingsWorker\s*\(\s*\)",
            r"void\s+WifiSettingsWorker::releaseRuntime\s*\(\s*\)",
        )
        release_runtime = method_body(
            worker,
            r"void\s+WifiSettingsWorker::releaseRuntime\s*\(\s*\)",
            r"void\s+WifiSettingsWorker::update\s*\(\s*\)",
        )

        self.assertLess(teardown.index("releaseRuntime()"), teardown.index("_view.reset()"))
        self.assertIn("_controller.reset()", release_runtime)

    def test_shared_driver_uses_existing_config_ap_start_stop_path(self):
        driver = read("main/runtime/services/stopwatch_wifi_driver.cpp")
        start_config_ap = method_body(
            driver,
            r"bool\s+startConfigAp\s*\(\s*\)\s+override",
            r"void\s+stopConfigAp\s*\(\s*\)\s+override",
        )
        stop_config_ap = method_body(
            driver,
            r"void\s+stopConfigAp\s*\(\s*\)\s+override",
            r"bool\s+staRequiredBySystem\s*\(\s*\)\s+const\s+override",
        )

        self.assertIn("wifi.StartConfigAp()", start_config_ap)
        self.assertIn("wifi.IsConfigMode()", start_config_ap)
        self.assertIn("WifiManager::GetInstance().StopConfigAp()", stop_config_ap)

    def test_assistant_and_other_wifi_consumers_keep_using_shared_service(self):
        consumers = {
            "assistant": ("main/runtime/assistant/assistant_runtime.cpp", "RuntimeOwner::Assistant"),
            "codex": ("main/apps/app_codex_status/app_codex_status.cpp", "RuntimeOwner::Codex"),
            "badge": ("main/hal/utils/config_ap/config_ap.cpp", "RuntimeOwner::Badge"),
        }

        for name, (path, owner) in consumers.items():
            source = read(path)
            self.assertIn("GetSharedWifiService()", source, name)
            self.assertIn(owner, source, name)

    def test_smartconfig_password_redaction_uses_a_generated_component_overlay(self):
        root_cmake = read("CMakeLists.txt")
        overlay = read("main/cmake/esp_wifi_connect_security_overlay.cmake")

        self.assertIn("esp_wifi_connect_security_overlay.cmake", root_cmake)
        self.assertIn("apply_esp_wifi_connect_security_overlay()", root_cmake)
        self.assertIn("78__esp-wifi-connect COMPONENT_LIB", overlay)
        self.assertIn("78__esp-wifi-connect COMPONENT_DIR", overlay)
        self.assertIn('"${wifi_connect_dir}/wifi_configuration_ap.cc"', overlay)
        self.assertIn('"${CMAKE_BINARY_DIR}/generated_overrides/esp_wifi_connect_security"', overlay)

        password_log = 'ESP_LOGI(TAG, "SmartConfig SSID: %s, Password: %s", ssid, password);'
        redacted_log = 'ESP_LOGI(TAG, "SmartConfig credentials received");'
        self.assertIn(password_log, overlay)
        self.assertIn(redacted_log, overlay)
        self.assertIn('string(FIND "${wifi_configuration_source}" "${required_text}"', overlay)
        self.assertIn('string(REPLACE "${smartconfig_password_log}" "${smartconfig_credentials_log}"', overlay)

        # These anchors keep the upstream credential-copy and Save behavior intact.
        self.assertIn('memcpy(password, evt->password, sizeof(evt->password));', overlay)
        self.assertIn('self->Save(ssid, password);', overlay)

        self.assertIn('list(REMOVE_ITEM wifi_connect_sources "${upstream_wifi_configuration}"', overlay)
        self.assertIn('set_property(TARGET ${wifi_connect_lib} PROPERTY SOURCES', overlay)
        self.assertIn('target_sources(${wifi_connect_lib} PRIVATE "${generated_wifi_configuration}")', overlay)

    def test_saved_network_order_drives_station_connection_priority(self):
        root_cmake = read("CMakeLists.txt")
        overlay = read("main/cmake/esp_wifi_connect_priority_overlay.cmake")
        priority = read("main/runtime/services/wifi_candidate_priority.h")

        self.assertIn("esp_wifi_connect_priority_overlay.cmake", root_cmake)
        self.assertIn("apply_esp_wifi_connect_priority_overlay()", root_cmake)
        self.assertIn('"${wifi_connect_dir}/wifi_station.cc"', overlay)
        self.assertIn("wifi_candidate_priority.h", overlay)
        self.assertIn("savedCandidatePriorityLess", overlay)
        self.assertIn("savedCandidatePriorityLess", priority)
        self.assertIn("connect_queue_.clear();", overlay)
        self.assertIn("this_->ssid_.clear();", overlay)

    def test_assistant_fails_closed_when_global_wifi_is_not_configured(self):
        service_header = read("main/runtime/services/shared_wifi_service.h")
        assistant = read("main/runtime/assistant/assistant_runtime.cpp")

        self.assertIn("acquireConfiguredStaDetailed", service_header)
        self.assertIn("acquireConfiguredStaDetailed(RuntimeOwner::Assistant)", assistant)
        self.assertIn("Configure Wi-Fi in Settings", assistant)
        self.assertIn("wifi_connect_timeout_ms", assistant)
        self.assertIn("Wi-Fi connection timed out", assistant)

    def test_wifi_rows_are_stable_across_rssi_only_updates_and_hidden_during_pairing(self):
        worker = read("main/apps/app_setup/workers/wifi.cpp")

        self.assertIn("_rendered_saved_ssids", worker)
        self.assertIn("snapshot.saved_ssids != _rendered_saved_ssids", worker)
        self.assertIn("_saved_list->setHidden(pairing)", worker)

    def test_wifi_runtime_cleanup_runs_outside_the_lvgl_lock(self):
        workers = read("main/apps/app_setup/workers/workers.h")
        app_setup = read("main/apps/app_setup/app_setup.cpp")
        wifi_worker = read("main/apps/app_setup/workers/wifi.cpp")

        on_running = method_body(
            app_setup,
            r"void\s+AppSetup::onRunning\s*\(\s*\)",
            r"void\s+AppSetup::onClose\s*\(\s*\)",
        )
        runtime_update = method_body(
            wifi_worker,
            r"void\s+WifiSettingsWorker::updateRuntime\s*\(\s*\)",
            r"void\s+WifiSettingsWorker::update\s*\(\s*\)",
        )
        ui_update = re.split(
            r"void\s+WifiSettingsWorker::update\s*\(\s*\)", wifi_worker, maxsplit=1
        )[1]

        self.assertIn("virtual void updateRuntime()", workers)
        self.assertIn("virtual void releaseRuntime()", workers)
        self.assertIn("void updateRuntime() override;", workers)
        self.assertIn("void releaseRuntime() override;", workers)
        self.assertLess(on_running.index("_worker->updateRuntime();"), on_running.index("LvglLockGuard lock;"))
        for runtime_operation in ("startPairing()", "stopPairing()", "selectSaved(", "poll()"):
            self.assertIn(runtime_operation, runtime_update)
            self.assertNotIn(runtime_operation, ui_update)
        self.assertIn("WifiSettingsWorker::releaseRuntime", wifi_worker)
        self.assertIn("_worker->releaseRuntime();", app_setup)


if __name__ == "__main__":
    unittest.main()
