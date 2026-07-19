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

echo "[1/9] Python contract and tooling tests"
"$PYTHON" -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'

echo "[2/9] Assistant state machine host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/assistant_state_machine_test.cpp \
    main/runtime/assistant/assistant_state.cpp \
    -o "$HOST_TEST_DIR/assistant_state_machine_test"
"$HOST_TEST_DIR/assistant_state_machine_test"

echo "[3/9] Assistant event mailbox host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/assistant_event_mailbox_test.cpp \
    main/runtime/assistant/assistant_event_mailbox.cpp \
    -o "$HOST_TEST_DIR/assistant_event_mailbox_test"
"$HOST_TEST_DIR/assistant_event_mailbox_test"

echo "[4/9] Resource supervisor host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/resource_supervisor_test.cpp \
    main/runtime/resource_supervisor.cpp \
    -o "$HOST_TEST_DIR/resource_supervisor_test"
"$HOST_TEST_DIR/resource_supervisor_test"

echo "[5/9] Shared Wi-Fi policy host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
    tests/shared_wifi_policy_test.cpp \
    main/runtime/resource_supervisor.cpp \
    main/runtime/services/shared_wifi_policy.cpp \
    main/runtime/services/shared_wifi_service.cpp \
    -o "$HOST_TEST_DIR/shared_wifi_policy_test"
"$HOST_TEST_DIR/shared_wifi_policy_test"

echo "[6/9] USB debug command host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -Imain \
    tests/debug_command_test.cpp \
    main/debug/debug_command_parser.cpp \
    main/debug/debug_app_control.cpp \
    -o "$HOST_TEST_DIR/debug_command_test"
"$HOST_TEST_DIR/debug_command_test"

echo "[7/9] Runtime diagnostics host test"
"$CXX" -std=c++17 -Wall -Wextra -Werror -DSTOPWATCH_HOST_TEST -Imain \
    tests/runtime_diagnostics_test.cpp \
    main/runtime/runtime_diagnostics.cpp \
    -o "$HOST_TEST_DIR/runtime_diagnostics_test"
"$HOST_TEST_DIR/runtime_diagnostics_test"

echo "[8/9] Isolated unified candidate clean build and capacity gates"
./scripts/build_unified_candidate.sh --clean

echo "[9/9] Whitespace error check"
git diff --check

echo "Unified offline validation passed. Hardware status is tracked separately in the validation reports."
