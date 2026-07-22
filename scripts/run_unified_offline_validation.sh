#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
PYTHON="${PYTHON:-$ROOT/mac-codex-agent/.venv/bin/python}"
HOST_TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/stopwatch-unified-validation.XXXXXX")"
trap 'rm -rf "$HOST_TEST_DIR"' EXIT

cd "$ROOT"

if [[ ! -x "$PYTHON" ]]; then
    echo "Python environment is missing: $PYTHON" >&2
    echo "Create mac-codex-agent/.venv and install mac-codex-agent/requirements.txt." >&2
    exit 2
fi

if ! command -v idf.py >/dev/null 2>&1; then
    IDF_EXPORT="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/export.sh"
    if [[ ! -f "$IDF_EXPORT" ]]; then
        echo "ESP-IDF environment is unavailable: $IDF_EXPORT" >&2
        exit 2
    fi
    # ESP-IDF's supported export script selects its managed Python environment
    # and toolchain before the isolated candidate build.
    # shellcheck disable=SC1090
    source "$IDF_EXPORT"
fi

echo "[1/20] Python contract and tooling tests"
"$PYTHON" -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'

echo "[2/20] Assistant state machine host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_state_machine_test.cpp \
    main/runtime/assistant/assistant_state.cpp \
    -o "$HOST_TEST_DIR/assistant_state_machine_test"
"$HOST_TEST_DIR/assistant_state_machine_test"

echo "[3/20] Assistant close controller host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_close_controller_test.cpp \
    main/apps/app_assistant/assistant_close_controller.cpp \
    -o "$HOST_TEST_DIR/assistant_close_controller_test"
"$HOST_TEST_DIR/assistant_close_controller_test"

echo "[4/20] Assistant event mailbox host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_event_mailbox_test.cpp \
    main/runtime/assistant/assistant_event_mailbox.cpp \
    -o "$HOST_TEST_DIR/assistant_event_mailbox_test"
"$HOST_TEST_DIR/assistant_event_mailbox_test"

echo "[5/20] Assistant update notice host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_update_notice_test.cpp \
    main/runtime/assistant/assistant_update_notice_policy.cpp \
    main/runtime/assistant/assistant_update_notice_bridge.cpp \
    -o "$HOST_TEST_DIR/assistant_update_notice_test"
"$HOST_TEST_DIR/assistant_update_notice_test"

echo "[6/20] Assistant trace host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/assistant_trace_test.cpp \
    main/runtime/assistant/assistant_trace.cpp \
    -o "$HOST_TEST_DIR/assistant_trace_test"
"$HOST_TEST_DIR/assistant_trace_test"

echo "[7/20] Resource supervisor host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/resource_supervisor_test.cpp \
    main/runtime/resource_supervisor.cpp \
    -o "$HOST_TEST_DIR/resource_supervisor_test"
"$HOST_TEST_DIR/resource_supervisor_test"

echo "[8/20] Button feedback policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/button_feedback_policy_test.cpp \
    main/hal/button_feedback_policy.cpp \
    -o "$HOST_TEST_DIR/button_feedback_policy_test"
"$HOST_TEST_DIR/button_feedback_policy_test"

echo "[9/20] Shared Wi-Fi policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/shared_wifi_policy_test.cpp \
    main/runtime/resource_supervisor.cpp \
    main/runtime/services/shared_wifi_policy.cpp \
    main/runtime/services/shared_wifi_service.cpp \
    -o "$HOST_TEST_DIR/shared_wifi_policy_test"
"$HOST_TEST_DIR/shared_wifi_policy_test"

echo "[10/20] WiFi settings model host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/wifi_settings_model_test.cpp \
    -o "$HOST_TEST_DIR/wifi_settings_model_test"
"$HOST_TEST_DIR/wifi_settings_model_test"

echo "[11/20] WiFi settings controller host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/wifi_settings_controller_test.cpp \
    main/apps/app_setup/workers/wifi_settings_controller.cpp \
    -o "$HOST_TEST_DIR/wifi_settings_controller_test"
"$HOST_TEST_DIR/wifi_settings_controller_test"

echo "[12/20] USB debug command host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/debug_command_test.cpp \
    main/debug/debug_command_parser.cpp \
    main/debug/debug_app_control.cpp \
    main/debug/debug_assistant_control.cpp \
    -o "$HOST_TEST_DIR/debug_command_test"
"$HOST_TEST_DIR/debug_command_test"

echo "[13/20] Runtime diagnostics host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/runtime_diagnostics_test.cpp \
    main/runtime/runtime_diagnostics.cpp \
    -o "$HOST_TEST_DIR/runtime_diagnostics_test"
"$HOST_TEST_DIR/runtime_diagnostics_test"

echo "[14/20] Audio jitter buffer policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/audio_jitter_buffer_policy_test.cpp \
    -o "$HOST_TEST_DIR/audio_jitter_buffer_policy_test"
"$HOST_TEST_DIR/audio_jitter_buffer_policy_test"

echo "[15/20] BLE voice probe protocol host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_protocol_test.cpp \
    main/apps/app_codex_status/ble_voice_probe_protocol.cpp \
    -o "$HOST_TEST_DIR/ble_voice_probe_protocol_test"
"$HOST_TEST_DIR/ble_voice_probe_protocol_test"

echo "[16/20] BLE voice probe session host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_session_test.cpp \
    main/apps/app_codex_status/ble_voice_probe_session.cpp \
    -o "$HOST_TEST_DIR/ble_voice_probe_session_test"
"$HOST_TEST_DIR/ble_voice_probe_session_test"

echo "[17/20] BLE voice probe PCM buffer host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_pcm_buffer_test.cpp \
    -o "$HOST_TEST_DIR/ble_voice_probe_pcm_buffer_test"
"$HOST_TEST_DIR/ble_voice_probe_pcm_buffer_test"

echo "[18/20] IMA ADPCM codec host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ima_adpcm_codec_test.cpp \
    main/apps/app_codex_status/ima_adpcm_codec.cpp \
    -o "$HOST_TEST_DIR/ima_adpcm_codec_test"
"$HOST_TEST_DIR/ima_adpcm_codec_test"

echo "[19/20] Isolated unified candidate clean build and capacity gates"
./scripts/build_unified_candidate.sh --clean

echo "[20/20] Whitespace error check"
git diff --check

echo "Unified offline validation passed. Hardware status is tracked separately in the validation reports."
