#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/stopwatch-clean-checkout.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
CLONE="$WORK_DIR/StopWatch"

if ! command -v idf.py >/dev/null 2>&1; then
    IDF_EXPORT="${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/export.sh"
    if [[ ! -f "$IDF_EXPORT" ]]; then
        echo "ESP-IDF environment is unavailable: $IDF_EXPORT" >&2
        exit 2
    fi
    # shellcheck disable=SC1090
    source "$IDF_EXPORT"
fi

export GIT_TERMINAL_PROMPT=0

echo "[1/4] Clone tracked project files"
git clone --no-local --quiet "$ROOT" "$CLONE"

echo "[2/4] Confirm generated dependencies are absent"
for generated in components managed_components third_party build build-unified; do
    if [[ -e "$CLONE/$generated" ]]; then
        echo "Clean clone unexpectedly contains $generated" >&2
        exit 2
    fi
done

cd "$CLONE"

echo "[3/4] Bootstrap locked dependencies"
python3 ./fetch_repos.py
python3 ./fetch_repos.py --check

echo "[4/4] Build unified firmware from scratch"
./scripts/build_unified_candidate.sh --clean

echo "Source commit:"
git rev-parse HEAD
echo "Artifact hashes:"
shasum -a 256 \
    build-unified/StopWatch-UserDemo.bin \
    build-unified/generated_assets.bin
echo "Reproducible clean-checkout validation passed."
