#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
PYTHON="${PYTHON:-$ROOT/mac-codex-agent/.venv/bin/python}"
TOTAL_STAGES=69
STAGE_INDEX=0
CONTRACT_ONLY=0
HOST_TESTS_ONLY=0
TRACE_FD=""

case "${1:-}" in
    "") ;;
    --contract) CONTRACT_ONLY=1 ;;
    --host-tests-only) HOST_TESTS_ONLY=1 ;;
    *)
        echo "Usage: $0 [--contract|--host-tests-only]" >&2
        exit 2
        ;;
esac

stage() {
    STAGE_INDEX=$((STAGE_INDEX + 1))
    printf '[%d/%d] %s\n' "$STAGE_INDEX" "$TOTAL_STAGES" "$1"
}

run_host_test() {
    local name="$1"
    shift
    if [[ "$CONTRACT_ONLY" -eq 1 ]]; then
        printf 'COMPILE\t%s' "$name"
        printf '\t%s' "$@"
        printf '\nRUN\t%s\n' "$name"
        return
    fi
    "$CXX" "$@" -o "$HOST_TEST_DIR/$name" 9>&-
    record_execution "COMPILED" "$name"
    "$HOST_TEST_DIR/$name" 9>&-
    record_execution "RAN" "$name"
}

open_execution_trace() {
    local trace_file="${STOPWATCH_VALIDATION_TRACE_FILE:-}"
    if [[ -z "$trace_file" || "$CONTRACT_ONLY" -eq 1 ]]; then
        return
    fi
    if [[ "$trace_file" == *$'\n'* || ! -d "$(dirname "$trace_file")" ]]; then
        echo "Validation trace path is invalid" >&2
        exit 2
    fi

    local previous_umask
    previous_umask="$(umask)"
    umask 077
    set -o noclobber
    if ! { exec 9>"$trace_file"; } 2>/dev/null; then
        set +o noclobber
        umask "$previous_umask"
        echo "Validation trace must be a new file: $trace_file" >&2
        exit 2
    fi
    set +o noclobber
    umask "$previous_umask"
    TRACE_FD=9
}

record_execution() {
    if [[ -n "$TRACE_FD" ]]; then
        printf '%s\t%s\n' "$1" "$2" >&9
    fi
}

verify_stage_count() {
    if [[ "$STAGE_INDEX" -ne "$TOTAL_STAGES" ]]; then
        echo "Validation stage mismatch: ran $STAGE_INDEX of $TOTAL_STAGES" >&2
        exit 2
    fi
}

cd "$ROOT"

if [[ "$CONTRACT_ONLY" -eq 0 ]]; then
    HOST_TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/stopwatch-unified-validation.XXXXXX")"
    trap 'rm -rf "$HOST_TEST_DIR"' EXIT
    open_execution_trace

    if [[ "$HOST_TESTS_ONLY" -eq 0 && ! -x "$PYTHON" ]]; then
        echo "Python environment is missing: $PYTHON" >&2
        echo "Create mac-codex-agent/.venv and install mac-codex-agent/requirements.txt." >&2
        exit 2
    fi

    if [[ "$HOST_TESTS_ONLY" -eq 0 ]]; then
        export STOPWATCH_BUILD_DIR="$HOST_TEST_DIR/firmware-build"
        export STOPWATCH_SDKCONFIG="$HOST_TEST_DIR/sdkconfig.unified"
        if ! command -v idf.py >/dev/null 2>&1; then
            IDF_EXPORT="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/export.sh"
            if [[ ! -f "$IDF_EXPORT" ]]; then
                echo "ESP-IDF environment is unavailable: $IDF_EXPORT" >&2
                exit 2
            fi
            # ESP-IDF's supported export script selects its managed Python
            # environment and toolchain before the isolated candidate build.
            # shellcheck disable=SC1090
            source "$IDF_EXPORT"
        fi
    else
        IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}"
    fi
else
    HOST_TEST_DIR="<contract-host-test-dir>"
    IDF_PATH="${IDF_PATH:-<idf-path>}"
fi

stage "Python contract and tooling tests"
if [[ "$CONTRACT_ONLY" -eq 0 && "$HOST_TESTS_ONLY" -eq 0 ]]; then
    "$PYTHON" -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
fi

stage "Assistant state machine host test"
run_host_test assistant_state_machine_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_state_machine_test.cpp \
    main/runtime/assistant/assistant_state.cpp

stage "Assistant presentation and lifecycle event policy host tests"
run_host_test assistant_presentation_model_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_presentation_model_test.cpp
run_host_test assistant_lifecycle_event_policy_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_lifecycle_event_policy_test.cpp

stage "Assistant audio level host test"
run_host_test assistant_audio_level_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_audio_level_test.cpp

stage "Assistant close controller host test"
run_host_test assistant_close_controller_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_close_controller_test.cpp \
    main/apps/app_assistant/assistant_close_controller.cpp

stage "Assistant event mailbox host test"
run_host_test assistant_event_mailbox_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_event_mailbox_test.cpp \
    main/runtime/assistant/assistant_event_mailbox.cpp

stage "Assistant update notice host test"
run_host_test assistant_update_notice_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_update_notice_test.cpp \
    main/runtime/assistant/assistant_update_notice_policy.cpp \
    main/runtime/assistant/assistant_update_notice_bridge.cpp

stage "Assistant trace host test"
run_host_test assistant_trace_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/assistant_trace_test.cpp \
    main/runtime/assistant/assistant_trace.cpp

stage "Resource supervisor host test"
run_host_test resource_supervisor_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/resource_supervisor_test.cpp \
    main/runtime/resource_supervisor.cpp

stage "Button feedback policy host test"
run_host_test button_feedback_policy_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/button_feedback_policy_test.cpp \
    main/hal/button_feedback_policy.cpp

stage "Shared Wi-Fi policy host test"
run_host_test shared_wifi_policy_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/shared_wifi_policy_test.cpp \
    main/runtime/resource_supervisor.cpp \
    main/runtime/services/shared_wifi_policy.cpp \
    main/runtime/services/shared_wifi_service.cpp

stage "WiFi settings model host test"
run_host_test wifi_settings_model_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/wifi_settings_model_test.cpp

stage "WiFi settings controller host test"
run_host_test wifi_settings_controller_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/wifi_settings_controller_test.cpp \
    main/apps/app_setup/workers/wifi_settings_controller.cpp

stage "USB debug command host test"
run_host_test debug_command_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/debug_command_test.cpp \
    main/debug/debug_command_parser.cpp \
    main/debug/debug_app_control.cpp \
    main/debug/debug_assistant_control.cpp

stage "Runtime diagnostics host test"
run_host_test runtime_diagnostics_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/runtime_diagnostics_test.cpp \
    main/runtime/runtime_diagnostics.cpp

stage "Audio jitter buffer policy host test"
run_host_test audio_jitter_buffer_policy_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/audio_jitter_buffer_policy_test.cpp

stage "Assistant output sync policy host test"
run_host_test assistant_output_sync_policy_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/assistant_output_sync_policy_test.cpp \
    main/runtime/assistant/assistant_output_sync_policy.cpp

stage "Global edge control host test"
run_host_test edge_control_logic_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/edge_control_logic_test.cpp \
    main/apps/common/edge_control/edge_control_logic.cpp

stage "Global low-battery alert host test"
run_host_test low_battery_alert_logic_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/low_battery_alert_logic_test.cpp \
    main/runtime/power/battery_percent_estimator.cpp \
    main/runtime/power/low_battery_alert_logic.cpp

TRAE_CJSON_DIR="${IDF_PATH}/components/json/cJSON"
TRAE_CJSON_OBJECT="$HOST_TEST_DIR/trae-cJSON.o"
if [[ "$CONTRACT_ONLY" -eq 0 ]]; then
    if [[ -f "$TRAE_CJSON_DIR/cJSON.c" ]]; then
        "${CC:-cc}" -Wall -Wextra -Werror -I"$TRAE_CJSON_DIR" \
            -c "$TRAE_CJSON_DIR/cJSON.c" -o "$TRAE_CJSON_OBJECT"
    else
        : > "$TRAE_CJSON_OBJECT"
    fi
fi

stage "Status message router host test"
run_host_test status_message_router_test \
    -std=c++17 -Wall -Wextra -Werror -Imain -I"$TRAE_CJSON_DIR" \
    tests/status_message_router_test.cpp \
    main/apps/app_codex_status/status_message_router.cpp \
    "$TRAE_CJSON_OBJECT"

stage "Trae status model host test"
run_host_test trae_status_model_test \
    -std=c++17 -Wall -Wextra -Werror -Imain -I"$TRAE_CJSON_DIR" \
    tests/trae_status_model_test.cpp \
    main/apps/app_codex_status/trae_status_model.cpp \
    "$TRAE_CJSON_OBJECT"

stage "Trae task carousel host test"
run_host_test trae_task_carousel_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_task_carousel_test.cpp \
    main/apps/app_codex_status/trae_task_carousel.cpp

stage "Codex status model compatibility host test"
run_host_test codex_status_model_test \
    -std=c++17 -Wall -Wextra -Werror \
    -Imain/apps/app_codex_status -I"$TRAE_CJSON_DIR" \
    tests/codex_status_model_test.cpp \
    main/apps/app_codex_status/codex_status_model.cpp \
    "$TRAE_CJSON_OBJECT"

stage "Codex button gesture host test"
run_host_test codex_button_gesture_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    tests/codex_button_gesture_test.cpp \
    main/apps/app_codex_status/codex_button_gesture.cpp

stage "Codex task detail replay and reassembly host test"
run_host_test codex_task_detail_store_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/codex_task_detail_store_test.cpp \
    main/apps/app_codex_status/codex_task_detail_store.cpp

stage "Codex task focus deck host test"
run_host_test codex_task_focus_deck_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_task_focus_deck_test.cpp \
    main/apps/app_codex_status/codex_task_focus_deck.cpp

stage "Codex task focus layout host test"
run_host_test codex_task_focus_layout_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_task_focus_layout_test.cpp

stage "Codex screen-off voice policy host test"
run_host_test codex_screen_off_voice_policy_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_screen_off_voice_policy_test.cpp

stage "Status page swipe and Trae card tap host tests"
run_host_test status_page_swipe_logic_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/status_page_swipe_logic_test.cpp \
    main/apps/app_codex_status/status_page_swipe_logic.cpp
run_host_test trae_card_tap_logic_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_card_tap_logic_test.cpp \
    main/apps/app_codex_status/trae_card_tap_logic.cpp

stage "Display protection configuration host test"
run_host_test display_protection_config_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/display_protection_config_test.cpp \
    main/runtime/display/display_protection_config.cpp

stage "AMOLED protection state machine host tests"
run_host_test display_protection_logic_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/display_protection_logic_test.cpp \
    main/runtime/display/display_protection_logic.cpp
run_host_test display_protection_diagnostics_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/display_protection_diagnostics_test.cpp \
    main/runtime/display/display_protection_diagnostics.cpp \
    main/runtime/display/display_protection_logic.cpp
run_host_test display_brightness_arbiter_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/display_brightness_arbiter_test.cpp \
    main/runtime/display/display_protection_logic.cpp
run_host_test screen_off_runtime_policy_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/screen_off_runtime_policy_test.cpp

stage "External power tracker host test"
run_host_test external_power_state_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/external_power_state_test.cpp

stage "Display protection settings model host test"
run_host_test display_protection_settings_model_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/display_protection_settings_model_test.cpp \
    main/runtime/display/display_protection_config.cpp

stage "Button and touch input wake gate host tests"
run_host_test button_input_gate_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/button_input_gate_test.cpp
run_host_test touch_input_gate_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/touch_input_gate_test.cpp \
    main/apps/common/edge_control/edge_control_logic.cpp \
    main/apps/app_codex_status/status_page_swipe_logic.cpp

stage "Assistant display activity host test"
run_host_test assistant_display_activity_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_display_activity_test.cpp

stage "Codex status UI host tests"
run_host_test codex_pending_glow_logic_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_pending_glow_logic_test.cpp
run_host_test codex_alert_carousel_test \
    -std=c++17 -Wall -Wextra -Werror -Imain/apps/app_codex_status \
    tests/codex_alert_carousel_test.cpp \
    main/apps/app_codex_status/codex_alert_carousel.cpp
run_host_test codex_status_ui_layout_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/codex_status_ui_layout_test.cpp

stage "Trae status UI layout host test"
run_host_test trae_status_ui_layout_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/trae_status_ui_layout_test.cpp

stage "BLE voice probe protocol host test"
run_host_test ble_voice_probe_protocol_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_protocol_test.cpp \
    main/apps/app_codex_status/ble_voice_probe_protocol.cpp

stage "BLE voice probe crypto host test"
MBEDTLS_SOURCE="$IDF_PATH/components/mbedtls/mbedtls"
MBEDTLS_BUILD="$HOST_TEST_DIR/mbedtls-host"
if [[ "$CONTRACT_ONLY" -eq 0 ]]; then
    cmake -S "$MBEDTLS_SOURCE" -B "$MBEDTLS_BUILD" \
        -DENABLE_PROGRAMS=OFF \
        -DENABLE_TESTING=OFF \
        -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
        -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build "$MBEDTLS_BUILD" --target mbedcrypto -j4
fi
run_host_test voice_probe_crypto_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/voice_probe_crypto_test.cpp \
    main/apps/app_codex_status/voice_probe_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Codex pet CXP1 package parser/decoder host test"
run_host_test codex_pet_package_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/codex_pet_package_test.cpp \
    main/apps/app_codex_status/codex_pet_package.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Codex pet two-slot persistence host test"
run_host_test codex_pet_store_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/codex_pet_store_test.cpp \
    main/apps/app_codex_status/codex_pet_store.cpp \
    main/apps/app_codex_status/codex_pet_package.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Codex pet authenticated control/status protocol host test"
run_host_test codex_pet_protocol_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/codex_pet_protocol_test.cpp \
    main/apps/app_codex_status/codex_pet_protocol.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Codex pet authenticated transfer controller host test"
run_host_test codex_pet_transfer_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/codex_pet_transfer_test.cpp \
    main/apps/app_codex_status/codex_pet_transfer.cpp \
    main/apps/app_codex_status/codex_pet_store.cpp \
    main/apps/app_codex_status/codex_pet_package.cpp \
    main/apps/app_codex_status/codex_pet_protocol.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Codex pet PSRAM animation runtime host test"
run_host_test codex_pet_runtime_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/codex_pet_runtime_test.cpp \
    main/apps/app_codex_status/codex_pet_runtime.cpp \
    main/apps/app_codex_status/codex_pet_package.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Secure Agent pairing protocol and crypto host tests"
run_host_test secure_pairing_crypto_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/secure_pairing_crypto_test.cpp \
    main/apps/app_codex_status/secure_pairing_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"
run_host_test secure_pairing_protocol_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/secure_pairing_protocol_test.cpp \
    main/apps/app_codex_status/secure_pairing_protocol.cpp \
    main/apps/app_codex_status/secure_pairing_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Authorized Agent registry host test"
run_host_test agent_registry_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/agent_registry_test.cpp \
    main/apps/app_codex_status/agent_registry.cpp \
    main/apps/app_codex_status/agent_registry_store.cpp \
    main/apps/app_codex_status/secure_pairing_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Secure Agent pairing session host test"
run_host_test secure_pairing_session_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/secure_pairing_session_test.cpp \
    main/apps/app_codex_status/secure_pairing_session.cpp \
    main/apps/app_codex_status/secure_pairing_protocol.cpp \
    main/apps/app_codex_status/secure_pairing_crypto.cpp \
    main/apps/app_codex_status/agent_registry.cpp \
    main/apps/app_codex_status/agent_registry_store.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Secure Agent pairing wire host test"
run_host_test secure_pairing_wire_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/secure_pairing_wire_test.cpp \
    main/apps/app_codex_status/secure_pairing_wire.cpp

stage "Agent connection router host test"
run_host_test agent_connection_router_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -I. \
    tests/agent_connection_router_test.cpp \
    main/apps/app_codex_status/agent_connection_router.cpp

stage "Agent panel model host test"
run_host_test agent_panel_model_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/agent_panel_model_test.cpp \
    main/apps/app_codex_status/agent_panel_model.cpp \
    main/apps/app_codex_status/agent_registry.cpp \
    main/apps/app_codex_status/secure_pairing_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Agent panel UI layout host test"
run_host_test agent_panel_ui_layout_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/agent_panel_ui_layout_test.cpp

stage "Agent panel presentation host test"
run_host_test agent_panel_presenter_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/agent_panel_presenter_test.cpp \
    main/apps/app_codex_status/agent_panel_presenter.cpp

stage "Production voice crypto host test"
run_host_test voice_input_crypto_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/voice_input_crypto_test.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Codex task detail crypto host test"
run_host_test codex_task_detail_crypto_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/codex_task_detail_crypto_test.cpp \
    main/apps/app_codex_status/codex_task_detail_crypto.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Production voice protocol host test"
run_host_test voice_input_protocol_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/voice_input_protocol_test.cpp \
    main/apps/app_codex_status/voice_input_protocol.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Input action protocol host test"
run_host_test input_action_protocol_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/input_action_protocol_test.cpp \
    main/apps/app_codex_status/input_action_protocol.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Input action controller host test"
run_host_test input_action_controller_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/input_action_controller_test.cpp \
    main/apps/app_codex_status/input_action_controller.cpp \
    main/apps/app_codex_status/input_action_protocol.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Production voice session host test"
run_host_test voice_input_session_test \
    -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -I. \
    tests/voice_input_session_test.cpp \
    main/apps/app_codex_status/voice_input_session.cpp

stage "Production voice capture pipeline host test"
run_host_test voice_capture_pipeline_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    tests/voice_capture_pipeline_test.cpp \
    main/apps/app_codex_status/voice_capture_pipeline.cpp

stage "Codex runtime session and pinned microphone host tests"
run_host_test codex_runtime_session_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/codex_runtime_session_test.cpp
run_host_test pinned_microphone_controller_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/pinned_microphone_controller_test.cpp \
    main/apps/app_codex_status/pinned_microphone_controller.cpp

stage "Production voice input controller host test"
run_host_test voice_input_controller_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/voice_input_controller_test.cpp \
    main/apps/app_codex_status/voice_input_controller.cpp \
    main/apps/app_codex_status/codex_button_gesture.cpp \
    main/apps/app_codex_status/voice_capture_pipeline.cpp \
    main/apps/app_codex_status/voice_input_session.cpp \
    main/apps/app_codex_status/voice_input_protocol.cpp \
    main/apps/app_codex_status/voice_input_crypto.cpp \
    "$MBEDTLS_BUILD/library/libmbedcrypto.a"

stage "Codex voice status priority host test"
run_host_test codex_status_priority_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/codex_status_priority_test.cpp

stage "BLE voice probe failure transition host test"
run_host_test ble_voice_probe_failure_transition_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_failure_transition_test.cpp \
    main/apps/app_codex_status/ble_voice_probe_failure_transition.cpp \
    main/apps/app_codex_status/ble_voice_probe_session.cpp \
    main/apps/app_codex_status/ble_voice_probe_protocol.cpp

stage "BLE voice probe session host test"
run_host_test ble_voice_probe_session_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_session_test.cpp \
    main/apps/app_codex_status/ble_voice_probe_session.cpp

stage "BLE voice probe PCM buffer host test"
run_host_test ble_voice_probe_pcm_buffer_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ble_voice_probe_pcm_buffer_test.cpp

stage "IMA ADPCM codec host test"
run_host_test ima_adpcm_codec_test \
    -std=c++17 -Wall -Wextra -Werror -I. \
    tests/ima_adpcm_codec_test.cpp \
    main/apps/app_codex_status/ima_adpcm_codec.cpp

stage "Native voice audio ring host test"
run_host_test voice_audio_ring_test \
    -std=c++17 -Wall -Wextra -Werror -Imac-codex-agent/native \
    mac-codex-agent/native/voice_audio_ring_test.cpp

stage "Isolated unified candidate clean build and capacity gates"
if [[ "$CONTRACT_ONLY" -eq 0 && "$HOST_TESTS_ONLY" -eq 0 ]]; then
    ./scripts/build_unified_candidate.sh --clean
fi

stage "Whitespace error check"
if [[ "$CONTRACT_ONLY" -eq 0 && "$HOST_TESTS_ONLY" -eq 0 ]]; then
    git diff --check
fi

verify_stage_count
if [[ "$CONTRACT_ONLY" -eq 0 && "$HOST_TESTS_ONLY" -eq 0 ]]; then
    echo "Unified offline validation passed. Hardware status is tracked separately in the validation reports."
elif [[ "$HOST_TESTS_ONLY" -eq 1 ]]; then
    echo "Unified host-test validation passed."
fi
