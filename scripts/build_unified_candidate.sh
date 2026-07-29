#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${STOPWATCH_BUILD_DIR:-$ROOT/build-unified}"
SDKCONFIG="${STOPWATCH_SDKCONFIG:-$ROOT/sdkconfig.unified}"
DEFAULTS="$ROOT/sdkconfig.defaults;$ROOT/sdkconfig.unified.defaults"
IDF_PY="${IDF_PY:-idf.py}"

if ! python3 "$ROOT/fetch_repos.py" --check; then
    echo "Source dependencies are missing or invalid." >&2
    echo "Run: python3 ./fetch_repos.py" >&2
    exit 2
fi

if [[ "${1:-}" == "--clean" && -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    "$IDF_PY" -B "$BUILD_DIR" fullclean
    rm -f "$SDKCONFIG"
fi

"$IDF_PY" -B "$BUILD_DIR" \
    -D "SDKCONFIG=$SDKCONFIG" \
    -D "SDKCONFIG_DEFAULTS=$DEFAULTS" reconfigure
"$IDF_PY" -B "$BUILD_DIR" build
python3 "$ROOT/scripts/report_firmware_metrics.py" \
    --repo-root "$ROOT" \
    --build-dir "$BUILD_DIR" \
    --partition-table "$ROOT/partitions.csv" \
    --assets-bin "$BUILD_DIR/generated_assets.bin" \
    --max-app-bytes 0x660000 \
    --max-assets-bytes 0xA0000 \
    --source unified-release-candidate \
    --output "$BUILD_DIR/unified-runtime-metrics.json"
