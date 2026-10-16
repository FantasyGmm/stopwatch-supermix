#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FUSION_ROOT="$(cd "$ROOT/.." && pwd)"
IDF_VERSION="v5.5.4"
IDF_COMMIT="735507283d5b2f9fb363a1901172dbd9e847945d"
IDF_PATH="$FUSION_ROOT/esp-idf-v5.5.4"
IDF_TOOLS_PATH="$FUSION_ROOT/idf-tools"
BUILD_DIR="${STOPWATCH_BUILD_DIR:-$ROOT/build-unified}"
SDKCONFIG="${STOPWATCH_SDKCONFIG:-$ROOT/sdkconfig.unified}"
DEFAULTS="$ROOT/sdkconfig.defaults;$ROOT/sdkconfig.unified.defaults"
LOG_DIR="$FUSION_ROOT/logs"
BUILD_LOG="$LOG_DIR/build-fusion-idf-v5.5.4.log"

mkdir -p "$LOG_DIR"
exec > >(tee -a "$BUILD_LOG") 2>&1
printf '\n[%s] ESP-IDF %s unified firmware build\n' \
    "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$IDF_VERSION"
printf 'Project: %s\nBuild directory: %s\nSDK config: %s\n' \
    "$ROOT" "$BUILD_DIR" "$SDKCONFIG"

if [[ "${1:-}" == "--clean" ]]; then
    CLEAN_BUILD=1
    shift
else
    CLEAN_BUILD=0
fi
if [[ "$#" -ne 0 ]]; then
    echo "Usage: $0 [--clean]" >&2
    exit 2
fi

if [[ ! -f "$IDF_PATH/export.sh" || ! -d "$IDF_PATH/.git" ]]; then
    echo "ESP-IDF $IDF_VERSION is not installed at $IDF_PATH." >&2
    echo "Run: $SCRIPT_DIR/install_idf_mirror.sh" >&2
    exit 2
fi

CHECKOUT_TAG="$(git -C "$IDF_PATH" describe --tags --exact-match HEAD 2>/dev/null || true)"
CHECKOUT_SHA="$(git -C "$IDF_PATH" rev-parse HEAD)"
if [[ "$CHECKOUT_TAG" != "$IDF_VERSION" || "$CHECKOUT_SHA" != "$IDF_COMMIT" ]]; then
    echo "Expected ESP-IDF $IDF_VERSION ($IDF_COMMIT); found ${CHECKOUT_TAG:-an untagged or modified checkout} ($CHECKOUT_SHA)." >&2
    exit 2
fi

SUBMODULE_STATUS="$(git -C "$IDF_PATH" submodule status --recursive)"
if [[ -z "$SUBMODULE_STATUS" ]] || ! awk 'substr($0, 1, 1) != " " { bad = 1; print; } END { exit bad; }' <<<"$SUBMODULE_STATUS"; then
    echo "ESP-IDF has an uninitialized or mismatched recursive submodule." >&2
    exit 2
fi
if [[ -n "$(git -C "$IDF_PATH" status --porcelain --untracked-files=all --ignore-submodules=none)" ]]; then
    echo "ESP-IDF source has local changes or untracked files; refusing to build." >&2
    git -C "$IDF_PATH" status --short --untracked-files=all --ignore-submodules=none >&2
    exit 2
fi
if ! git -C "$IDF_PATH" submodule foreach --quiet --recursive \
    'test -z "$(git status --porcelain --untracked-files=all)"'; then
    echo "An ESP-IDF recursive submodule has local changes or untracked files; refusing to build." >&2
    exit 2
fi
echo "Verified ESP-IDF source: $CHECKOUT_TAG ($CHECKOUT_SHA), recursive submodules initialized and clean."

export IDF_PATH
export IDF_TOOLS_PATH
export IDF_TARGET=esp32s3
export IDF_GITHUB_ASSETS="dl.espressif.cn/github_assets"
export PIP_INDEX_URL="https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple"
export PIP_CACHE_DIR="$IDF_TOOLS_PATH/pip-cache"
export TMPDIR="$IDF_TOOLS_PATH/tmp"
export PYTHONDONTWRITEBYTECODE=1
mkdir -p "$PIP_CACHE_DIR"
mkdir -p "$TMPDIR"
# shellcheck disable=SC1091
source "$IDF_PATH/export.sh"

if ! python3 "$ROOT/fetch_repos.py" --check; then
    echo "Pinned source dependencies are missing or invalid." >&2
    echo "Materialize them once with: cd \"$ROOT\" && python3 ./fetch_repos.py" >&2
    exit 2
fi

if [[ "$CLEAN_BUILD" -eq 1 && -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    idf.py -B "$BUILD_DIR" fullclean
    rm -f "$SDKCONFIG"
fi

# These are the same unified candidate configuration and capacity limits used
# by build_unified_candidate.sh. This script deliberately has no flash command.
cd "$ROOT"
idf.py -B "$BUILD_DIR" \
    -D "SDKCONFIG=$SDKCONFIG" \
    -D "SDKCONFIG_DEFAULTS=$DEFAULTS" reconfigure
idf.py -B "$BUILD_DIR" build
python3 "$ROOT/scripts/report_firmware_metrics.py" \
    --repo-root "$ROOT" \
    --build-dir "$BUILD_DIR" \
    --partition-table "$ROOT/partitions.csv" \
    --assets-bin "$BUILD_DIR/generated_assets.bin" \
    --max-app-bytes 0x700000 \
    --max-assets-bytes 0xA0000 \
    --source unified-release-candidate \
    --output "$BUILD_DIR/unified-runtime-metrics.json"

python3 - "$BUILD_DIR/unified-runtime-metrics.json" "$SDKCONFIG" <<'PY'
import json
import pathlib
import sys

metrics = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
config_path = pathlib.Path(sys.argv[2])
config = config_path.read_text(encoding="utf-8")
keys = (
    "CONFIG_SPIRAM",
    "CONFIG_SPIRAM_MODE_OCT",
    "CONFIG_SPIRAM_SPEED_80M",
    "CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL",
    "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL",
    "CONFIG_ESP_MAIN_TASK_STACK_SIZE",
    "CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE",
    "CONFIG_FREERTOS_HZ",
)
settings = {
    line.split("=", 1)[0]: line.split("=", 1)[1]
    for line in config.splitlines()
    if line.startswith("CONFIG_") and "=" in line
}
print("Firmware artifact and memory metrics:")
for key in (
    "app_bin",
    "app_bytes",
    "app_sha256",
    "app_partition_label",
    "app_partition_bytes",
    "app_headroom_bytes",
    "diram_bytes",
    "assets_bytes",
    "assets_sha256",
    "capacity_gate_passed",
):
    print(f"  {key}: {metrics.get(key)}")
print(f"SDK config: {config_path}")
for key in keys:
    print(f"  {key}: {settings.get(key, 'unset')}")
PY
