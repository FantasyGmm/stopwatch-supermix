#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX="${CXX:-c++}"
PYTHON="${PYTHON:-$ROOT/mac-codex-agent/.venv/bin/python}"
TOTAL_STAGES=29
STAGE_INDEX=0
CONTRACT_ONLY=0

case "${1:-}" in
    "") ;;
    --contract) CONTRACT_ONLY=1 ;;
    *)
        echo "Usage: $0 [--contract]" >&2
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
    "$CXX" "$@" -o "$HOST_TEST_DIR/$name"
    "$HOST_TEST_DIR/$name"
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

    if [[ ! -x "$PYTHON" ]]; then
        echo "Python environment is missing: $PYTHON" >&2
        echo "Create mac-codex-agent/.venv and install mac-codex-agent/requirements.txt." >&2
        exit 2
    fi

    export STOPWATCH_VOICE_KEY_FILE="$HOST_TEST_DIR/device-key.hex"
    export STOPWATCH_BUILD_DIR="$HOST_TEST_DIR/firmware-build"
    export STOPWATCH_SDKCONFIG="$HOST_TEST_DIR/sdkconfig.unified"
    (
        umask 077
        "$PYTHON" - <<'PY'
import os
import pathlib
import secrets

path = pathlib.Path(os.environ["STOPWATCH_VOICE_KEY_FILE"])
path.write_text(secrets.token_hex(16) + "\n", encoding="ascii")
PY
    )

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
    HOST_TEST_DIR="<contract-host-test-dir>"
    IDF_PATH="${IDF_PATH:-<idf-path>}"
fi

stage "Python contract and tooling tests"
if [[ "$CONTRACT_ONLY" -eq 0 ]]; then
    "$PYTHON" -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
fi

stage "Assistant state machine host test"
run_host_test assistant_state_machine_test \
    -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_state_machine_test.cpp \
    main/runtime/assistant/assistant_state.cpp

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

stage "Production voice crypto host test"
run_host_test voice_input_crypto_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/voice_input_crypto_test.cpp \
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

stage "Production voice input controller host test"
run_host_test voice_input_controller_test \
    -std=c++17 -Wall -Wextra -Werror -pthread -DSTOPWATCH_HOST_TEST -I. \
    -I"$MBEDTLS_SOURCE/include" \
    tests/voice_input_controller_test.cpp \
    main/apps/app_codex_status/voice_input_controller.cpp \
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
if [[ "$CONTRACT_ONLY" -eq 0 ]]; then
    ./scripts/build_unified_candidate.sh --clean
fi

stage "Whitespace error check"
if [[ "$CONTRACT_ONLY" -eq 0 ]]; then
    git diff --check
fi

verify_stage_count
if [[ "$CONTRACT_ONLY" -eq 0 ]]; then
    echo "Unified offline validation passed. Hardware status is tracked separately in the validation reports."
fi
