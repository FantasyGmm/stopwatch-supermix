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

echo "[1/27] Python contract and tooling tests"
"$PYTHON" -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'

echo "[2/27] Assistant state machine host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_state_machine_test.cpp \
    main/runtime/assistant/assistant_state.cpp \
    -o "$HOST_TEST_DIR/assistant_state_machine_test"
"$HOST_TEST_DIR/assistant_state_machine_test"

echo "[3/27] Assistant presentation and lifecycle event policy host tests"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_presentation_model_test.cpp \
    -o "$HOST_TEST_DIR/assistant_presentation_model_test"
"$HOST_TEST_DIR/assistant_presentation_model_test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_lifecycle_event_policy_test.cpp \
    -o "$HOST_TEST_DIR/assistant_lifecycle_event_policy_test"
"$HOST_TEST_DIR/assistant_lifecycle_event_policy_test"

echo "[4/27] Assistant audio level host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_audio_level_test.cpp \
    -o "$HOST_TEST_DIR/assistant_audio_level_test"
"$HOST_TEST_DIR/assistant_audio_level_test"

echo "[5/27] Assistant close controller host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_close_controller_test.cpp \
    main/apps/app_assistant/assistant_close_controller.cpp \
    -o "$HOST_TEST_DIR/assistant_close_controller_test"
"$HOST_TEST_DIR/assistant_close_controller_test"

echo "[6/27] Assistant event mailbox host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_event_mailbox_test.cpp \
    main/runtime/assistant/assistant_event_mailbox.cpp \
    -o "$HOST_TEST_DIR/assistant_event_mailbox_test"
"$HOST_TEST_DIR/assistant_event_mailbox_test"

echo "[7/27] Assistant update notice host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_update_notice_test.cpp \
    main/runtime/assistant/assistant_update_notice_policy.cpp \
    main/runtime/assistant/assistant_update_notice_bridge.cpp \
    -o "$HOST_TEST_DIR/assistant_update_notice_test"
"$HOST_TEST_DIR/assistant_update_notice_test"

echo "[8/27] Assistant trace host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/assistant_trace_test.cpp \
    main/runtime/assistant/assistant_trace.cpp \
    -o "$HOST_TEST_DIR/assistant_trace_test"
"$HOST_TEST_DIR/assistant_trace_test"

echo "[9/27] Resource supervisor host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/resource_supervisor_test.cpp \
    main/runtime/resource_supervisor.cpp \
    -o "$HOST_TEST_DIR/resource_supervisor_test"
"$HOST_TEST_DIR/resource_supervisor_test"

echo "[10/27] Button feedback policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/button_feedback_policy_test.cpp \
    main/hal/button_feedback_policy.cpp \
    -o "$HOST_TEST_DIR/button_feedback_policy_test"
"$HOST_TEST_DIR/button_feedback_policy_test"

echo "[11/27] Shared Wi-Fi policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/shared_wifi_policy_test.cpp \
    main/runtime/resource_supervisor.cpp \
    main/runtime/services/shared_wifi_policy.cpp \
    main/runtime/services/shared_wifi_service.cpp \
    -o "$HOST_TEST_DIR/shared_wifi_policy_test"
"$HOST_TEST_DIR/shared_wifi_policy_test"

echo "[12/27] WiFi settings model host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/wifi_settings_model_test.cpp \
    -o "$HOST_TEST_DIR/wifi_settings_model_test"
"$HOST_TEST_DIR/wifi_settings_model_test"

echo "[13/27] WiFi settings controller host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/wifi_settings_controller_test.cpp \
    main/apps/app_setup/workers/wifi_settings_controller.cpp \
    -o "$HOST_TEST_DIR/wifi_settings_controller_test"
"$HOST_TEST_DIR/wifi_settings_controller_test"

echo "[14/27] USB debug command host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/debug_command_test.cpp \
    main/debug/debug_command_parser.cpp \
    main/debug/debug_app_control.cpp \
    main/debug/debug_assistant_control.cpp \
    -o "$HOST_TEST_DIR/debug_command_test"
"$HOST_TEST_DIR/debug_command_test"

echo "[15/27] Runtime diagnostics host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/runtime_diagnostics_test.cpp \
    main/runtime/runtime_diagnostics.cpp \
    -o "$HOST_TEST_DIR/runtime_diagnostics_test"
"$HOST_TEST_DIR/runtime_diagnostics_test"

echo "[16/27] Audio jitter buffer policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/audio_jitter_buffer_policy_test.cpp \
    -o "$HOST_TEST_DIR/audio_jitter_buffer_policy_test"
"$HOST_TEST_DIR/audio_jitter_buffer_policy_test"

echo "[17/27] Assistant output sync policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/assistant_output_sync_policy_test.cpp \
    main/runtime/assistant/assistant_output_sync_policy.cpp \
    -o "$HOST_TEST_DIR/assistant_output_sync_policy_test"
"$HOST_TEST_DIR/assistant_output_sync_policy_test"

echo "[18/27] Global edge control host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I. \
    tests/edge_control_logic_test.cpp \
    main/apps/common/edge_control/edge_control_logic.cpp \
    -o "$HOST_TEST_DIR/edge_control_logic_test"
"$HOST_TEST_DIR/edge_control_logic_test"

TRAE_CJSON_DIR="${IDF_PATH}/components/json/cJSON"
"${CC:-cc}" -Wall -Wextra -Werror -I"$TRAE_CJSON_DIR" \
    -c "$TRAE_CJSON_DIR/cJSON.c" -o "$HOST_TEST_DIR/trae-cJSON.o"

echo "[19/27] Status message router host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain -I"$TRAE_CJSON_DIR" \
    tests/status_message_router_test.cpp \
    main/apps/app_codex_status/status_message_router.cpp \
    "$HOST_TEST_DIR/trae-cJSON.o" \
    -o "$HOST_TEST_DIR/status_message_router_test"
"$HOST_TEST_DIR/status_message_router_test"

echo "[20/27] Trae status model host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain -I"$TRAE_CJSON_DIR" \
    tests/trae_status_model_test.cpp \
    main/apps/app_codex_status/trae_status_model.cpp \
    "$HOST_TEST_DIR/trae-cJSON.o" \
    -o "$HOST_TEST_DIR/trae_status_model_test"
"$HOST_TEST_DIR/trae_status_model_test"

echo "[21/27] Trae task carousel host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_task_carousel_test.cpp \
    main/apps/app_codex_status/trae_task_carousel.cpp \
    -o "$HOST_TEST_DIR/trae_task_carousel_test"
"$HOST_TEST_DIR/trae_task_carousel_test"

echo "[22/27] Codex status model compatibility host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror \
    -Imain/apps/app_codex_status -I"$TRAE_CJSON_DIR" \
    tests/codex_status_model_test.cpp \
    main/apps/app_codex_status/codex_status_model.cpp \
    "$HOST_TEST_DIR/trae-cJSON.o" \
    -o "$HOST_TEST_DIR/codex_status_model_test"
"$HOST_TEST_DIR/codex_status_model_test"

echo "[23/27] Status page swipe and Pending glow logic host tests"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/status_page_swipe_logic_test.cpp \
    main/apps/app_codex_status/status_page_swipe_logic.cpp \
    -o "$HOST_TEST_DIR/status_page_swipe_logic_test"
"$HOST_TEST_DIR/status_page_swipe_logic_test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_card_tap_logic_test.cpp \
    main/apps/app_codex_status/trae_card_tap_logic.cpp \
    -o "$HOST_TEST_DIR/trae_card_tap_logic_test"
"$HOST_TEST_DIR/trae_card_tap_logic_test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_pending_glow_logic_test.cpp \
    -o "$HOST_TEST_DIR/codex_pending_glow_logic_test"
"$HOST_TEST_DIR/codex_pending_glow_logic_test"

echo "[24/27] Codex status UI layout host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_status_ui_layout_test.cpp \
    -o "$HOST_TEST_DIR/codex_status_ui_layout_test"
"$HOST_TEST_DIR/codex_status_ui_layout_test"

echo "[25/27] Trae status UI layout host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_status_ui_layout_test.cpp \
    -o "$HOST_TEST_DIR/trae_status_ui_layout_test"
"$HOST_TEST_DIR/trae_status_ui_layout_test"

echo "[26/27] Isolated unified candidate clean build and capacity gates"
./scripts/build_unified_candidate.sh --clean

echo "[27/27] Whitespace error check"
git diff --check

echo "Unified offline validation passed. Hardware status is tracked separately in the validation reports."
