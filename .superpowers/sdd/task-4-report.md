# Task 4 Report: Global WiFi Settings UI

Status: DONE_WITH_CONCERNS

Commit: `4cb6247 Add global WiFi Settings page`

## Delivered

- Added the `WiFi` Settings section between `Time & Date` and `Firmware`.
- Added `WifiSettingsWorker` with controller-first teardown, view request consumption on the main/LVGL thread, 500 ms snapshot polling, and changed-snapshot rendering.
- Added a 466 x 466 black round-screen page with a 350 px vertical saved-network list, English-only status text, default-network marker, connected-network blue state, pairing AP details, and `Add WiFi`, `Stop Pairing`, and `Done` actions.
- Added a focused firmware contract that verifies the menu entry, controller-owned UI, polling, teardown order, required labels, and absence of `WifiManager` / `SetEventCallback` in `wifi.cpp`.

## Validation

- `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_unified_runtime_firmware_contract`: passed, 14 tests.
- `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test && /tmp/wifi_settings_model_test`: passed.
- `c++ -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain tests/wifi_settings_controller_test.cpp main/apps/app_setup/workers/wifi_settings_controller.cpp -o /tmp/wifi_settings_controller_test && /tmp/wifi_settings_controller_test`: passed.
- `idf.py build`: passed after the targeted source was compiled. Artifact: `build/StopWatch-UserDemo.bin`, `0x5d12a0` bytes; smallest app partition has `0x1aed60` bytes (22%) free.
- `git diff --check` and `git diff --cached --check`: no output.

## Build Diagnostics

The first ESP-IDF attempt reached `wifi.cpp` and reported two Task 4 source issues: missing `assets/assets.h` for `MontserratSemiBold26`, and unavailable `lv_font_montserrat_12`. Both were fixed by adding the assets header and using `lv_font_montserrat_14` for the default marker. The subsequent build completed successfully.

## Concerns

- No on-device touch or visual validation was performed. The saved-list scrolling, round-screen placement, pairing portal interaction, and Done return path still need hardware validation.
- An accidental broad offline-validation invocation encountered an existing dirty-worktree failure in `test_audio_service_firmware_contract.py` expecting `AudioHelper::FunctionId::SetMute`. This was outside Task 4; the requested focused contract and host tests pass.
- The commit deliberately excludes all unrelated dirty files, including the pre-existing modifications in `mac-codex-agent/tests/test_unified_runtime_firmware_contract.py`.

## Review-Fix Evidence (2026-07-22)

- Connected state now renders `Connected: <snapshot.ssid>` and a separate `IP: <snapshot.ip_address>  RSSI: <snapshot.rssi> dBm` line. Connecting state renders `Connecting: <snapshot.ssid>`.
- Pairing now uses distinct full-width `AP SSID: <snapshot.ap_ssid>` and `Portal: <snapshot.ap_url>` labels at y=104 and y=128. Both use circular scrolling rather than ellipsis; the fixed 140 px list still begins at y=150.
- The first saved-network SSID has a 224 px label width and a 12 px gap before the 78 px `Default` marker. Non-default rows retain a 310 px label width.
- Red test: the new `test_settings_wifi_page_renders_connection_and_pairing_snapshot_details` initially failed because `wifi.cpp` did not reference `snapshot.ip_address`.
- `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_unified_runtime_firmware_contract -v`: passed, 15 tests.
- `c++ -std=c++17 -Wall -Wextra -Werror -Imain tests/wifi_settings_model_test.cpp -o /tmp/wifi_settings_model_test && /tmp/wifi_settings_model_test`: passed.
- `c++ -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain tests/wifi_settings_controller_test.cpp main/apps/app_setup/workers/wifi_settings_controller.cpp -o /tmp/wifi_settings_controller_test && /tmp/wifi_settings_controller_test`: passed.
- `source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh >/dev/null && idf.py build`: passed (exit 0). Artifact `build/StopWatch-UserDemo.bin` is `0x5d16e0`; the smallest `0x780000` app partition has `0x1ae920` (22%) free.
- `wifi.cpp` itself compiled after this fix: `build/.ninja_log` records `esp-idf/main/CMakeFiles/__idf_main.dir/apps/app_setup/workers/wifi.cpp.obj` completing at 03:29:42, and the object file has the same timestamp before the successful link.
- `git diff --check` and `git diff --cached --check`: no output before staging the isolated Task 4 hunk set.

## Final Layout-Finding Evidence (2026-07-22)

- Saved-network rows now call `row->setPaddingAll(0);` after the compact button style and before aligning the SSID label or `Default` marker, preventing LVGL default theme padding from changing the intended row geometry.
- Added isolated contract `mac-codex-agent/tests/test_wifi_settings_layout_firmware_contract.py`; its red run failed on the missing padding reset, and its green run passed after the fix. It also asserts `kSsidMarkerGap = 12` and the required source ordering.
- `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_wifi_settings_layout_firmware_contract`: passed, 1 test.
- `mac-codex-agent/.venv/bin/python -m unittest mac-codex-agent.tests.test_unified_runtime_firmware_contract`: passed, 15 tests.
- `source /Users/bytedance/esp/esp-idf-v5.5.4/export.sh >/dev/null && idf.py build`: passed (exit 0); `wifi.cpp` compiled, `build/StopWatch-UserDemo.bin` is `0x5d16e0`, and the smallest `0x780000` app partition has `0x1ae920` (22%) free.
- Scoped `git diff --check`: passed with no output. No on-device visual or touch validation was performed.
