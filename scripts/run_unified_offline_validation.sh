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

echo "[1/21] Python contract and tooling tests"
"$PYTHON" -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'

echo "[2/21] Assistant state machine host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_state_machine_test.cpp \
    main/runtime/assistant/assistant_state.cpp \
    -o "$HOST_TEST_DIR/assistant_state_machine_test"
"$HOST_TEST_DIR/assistant_state_machine_test"

echo "[3/21] Assistant presentation and lifecycle event policy host tests"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_presentation_model_test.cpp \
    -o "$HOST_TEST_DIR/assistant_presentation_model_test"
"$HOST_TEST_DIR/assistant_presentation_model_test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_lifecycle_event_policy_test.cpp \
    -o "$HOST_TEST_DIR/assistant_lifecycle_event_policy_test"
"$HOST_TEST_DIR/assistant_lifecycle_event_policy_test"

echo "[4/21] Assistant audio level host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_audio_level_test.cpp \
    -o "$HOST_TEST_DIR/assistant_audio_level_test"
"$HOST_TEST_DIR/assistant_audio_level_test"

echo "[5/21] Assistant close controller host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_close_controller_test.cpp \
    main/apps/app_assistant/assistant_close_controller.cpp \
    -o "$HOST_TEST_DIR/assistant_close_controller_test"
"$HOST_TEST_DIR/assistant_close_controller_test"

echo "[6/21] Assistant event mailbox host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_event_mailbox_test.cpp \
    main/runtime/assistant/assistant_event_mailbox.cpp \
    -o "$HOST_TEST_DIR/assistant_event_mailbox_test"
"$HOST_TEST_DIR/assistant_event_mailbox_test"

echo "[7/21] Assistant update notice host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_update_notice_test.cpp \
    main/runtime/assistant/assistant_update_notice_policy.cpp \
    main/runtime/assistant/assistant_update_notice_bridge.cpp \
    -o "$HOST_TEST_DIR/assistant_update_notice_test"
"$HOST_TEST_DIR/assistant_update_notice_test"

echo "[8/21] Assistant trace host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/assistant_trace_test.cpp \
    main/runtime/assistant/assistant_trace.cpp \
    -o "$HOST_TEST_DIR/assistant_trace_test"
"$HOST_TEST_DIR/assistant_trace_test"

echo "[9/21] Resource supervisor host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/resource_supervisor_test.cpp \
    main/runtime/resource_supervisor.cpp \
    -o "$HOST_TEST_DIR/resource_supervisor_test"
"$HOST_TEST_DIR/resource_supervisor_test"

echo "[10/21] Button feedback policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/button_feedback_policy_test.cpp \
    main/hal/button_feedback_policy.cpp \
    -o "$HOST_TEST_DIR/button_feedback_policy_test"
"$HOST_TEST_DIR/button_feedback_policy_test"

echo "[11/21] Shared Wi-Fi policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/shared_wifi_policy_test.cpp \
    main/runtime/resource_supervisor.cpp \
    main/runtime/services/shared_wifi_policy.cpp \
    main/runtime/services/shared_wifi_service.cpp \
    -o "$HOST_TEST_DIR/shared_wifi_policy_test"
"$HOST_TEST_DIR/shared_wifi_policy_test"

echo "[12/21] WiFi settings model host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/wifi_settings_model_test.cpp \
    -o "$HOST_TEST_DIR/wifi_settings_model_test"
"$HOST_TEST_DIR/wifi_settings_model_test"

echo "[13/21] WiFi settings controller host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/wifi_settings_controller_test.cpp \
    main/apps/app_setup/workers/wifi_settings_controller.cpp \
    -o "$HOST_TEST_DIR/wifi_settings_controller_test"
"$HOST_TEST_DIR/wifi_settings_controller_test"

echo "[14/21] USB debug command host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/debug_command_test.cpp \
    main/debug/debug_command_parser.cpp \
    main/debug/debug_app_control.cpp \
    main/debug/debug_assistant_control.cpp \
    -o "$HOST_TEST_DIR/debug_command_test"
"$HOST_TEST_DIR/debug_command_test"

echo "[15/21] Runtime diagnostics host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/runtime_diagnostics_test.cpp \
    main/runtime/runtime_diagnostics.cpp \
    -o "$HOST_TEST_DIR/runtime_diagnostics_test"
"$HOST_TEST_DIR/runtime_diagnostics_test"

echo "[16/21] Audio jitter buffer policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/audio_jitter_buffer_policy_test.cpp \
    -o "$HOST_TEST_DIR/audio_jitter_buffer_policy_test"
"$HOST_TEST_DIR/audio_jitter_buffer_policy_test"

echo "[17/21] Assistant output sync policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/assistant_output_sync_policy_test.cpp \
    main/runtime/assistant/assistant_output_sync_policy.cpp \
    -o "$HOST_TEST_DIR/assistant_output_sync_policy_test"
"$HOST_TEST_DIR/assistant_output_sync_policy_test"

echo "[18/21] Global edge control host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/edge_control_logic_test.cpp \
    main/apps/common/edge_control/edge_control_logic.cpp \
    -o "$HOST_TEST_DIR/edge_control_logic_test"
"$HOST_TEST_DIR/edge_control_logic_test"

echo "[19/21] Global low-battery alert host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/low_battery_alert_logic_test.cpp \
    main/runtime/power/low_battery_alert_logic.cpp \
    -o "$HOST_TEST_DIR/low_battery_alert_logic_test"
"$HOST_TEST_DIR/low_battery_alert_logic_test"

echo "[20/21] Codex status UI host tests"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_pending_glow_logic_test.cpp \
    -o "$HOST_TEST_DIR/codex_pending_glow_logic_test"
"$HOST_TEST_DIR/codex_pending_glow_logic_test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain/apps/app_codex_status \
    tests/codex_alert_carousel_test.cpp \
    main/apps/app_codex_status/codex_alert_carousel.cpp \
    -o "$HOST_TEST_DIR/codex_alert_carousel_test"
"$HOST_TEST_DIR/codex_alert_carousel_test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_status_ui_layout_test.cpp \
    -o "$HOST_TEST_DIR/codex_status_ui_layout_test"
"$HOST_TEST_DIR/codex_status_ui_layout_test"

echo "[21/21] Isolated unified candidate clean build and capacity gates"
./scripts/build_unified_candidate.sh --clean

echo "Whitespace error check"
git diff --check

echo "Unified offline validation passed. Hardware status is tracked separately in the validation reports."
