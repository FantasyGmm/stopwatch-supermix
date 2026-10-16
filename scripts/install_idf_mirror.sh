#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FUSION_ROOT="$(cd "$PROJECT_DIR/.." && pwd)"
IDF_VERSION="v5.5.4"
IDF_COMMIT="735507283d5b2f9fb363a1901172dbd9e847945d"
IDF_DIR="$FUSION_ROOT/esp-idf-v5.5.4"
TOOLS_DIR="$FUSION_ROOT/idf-tools"
WORK_DIR="$FUSION_ROOT/work"
LOG_DIR="$FUSION_ROOT/logs"
LOG_FILE="$LOG_DIR/install-idf-v5.5.4.log"
ARCHIVE_URL="https://dl.espressif.cn/github_assets/espressif/esp-idf/releases/download/v5.5.4/esp-idf-v5.5.4.zip"
ARCHIVE_FILE="$WORK_DIR/esp-idf-v5.5.4.zip"
ARCHIVE_PART="$ARCHIVE_FILE.part"
ARCHIVE_SHA256="3992025e4e4b6e9401235e95264936ab887dca975e8ebf6f331173f6e71734f9"
ARCHIVE_BYTES="1829210055"
INITIAL_MIRROR_URL="https://gitee.com/EspressifSystems/esp-idf.git"
INITIAL_GITEE_BACKUP="$WORK_DIR/idf-initial-gitee"

mkdir -p "$LOG_DIR" "$TOOLS_DIR" "$WORK_DIR"
exec > >(tee -a "$LOG_FILE") 2>&1

printf '\n[%s] ESP-IDF %s mirror setup\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')" "$IDF_VERSION"
printf 'Project: %s\nIDF source: %s\nIDF tools: %s\n' \
    "$PROJECT_DIR" "$ARCHIVE_URL" "$TOOLS_DIR"

if [[ "$(uname -s)" != "Darwin" || "$(uname -m)" != "arm64" ]]; then
    echo "This installer is configured for arm64 macOS." >&2
    exit 2
fi

python3 - <<'PY'
import sys
if sys.version_info[:2] != (3, 12):
    raise SystemExit("Python 3.12 is required; found {}.{}.{}".format(*sys.version_info[:3]))
print("Base Python: {}.{}.{} ({})".format(*sys.version_info[:3], sys.executable))
PY

checkout_error() {
    local checkout="$1"
    local expected_tag actual_tag actual_sha submodules

    if ! git -C "$checkout" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "ESP-IDF source is not a Git repository: $checkout" >&2
        return 1
    fi

    expected_tag="$(git -C "$checkout" describe --tags --exact-match HEAD 2>/dev/null || true)"
    actual_sha="$(git -C "$checkout" rev-parse HEAD)"
    if [[ "$expected_tag" != "$IDF_VERSION" || "$actual_sha" != "$IDF_COMMIT" ]]; then
        echo "ESP-IDF source must be $IDF_VERSION ($IDF_COMMIT); found ${expected_tag:-untagged} ($actual_sha)." >&2
        return 1
    fi

    if ! submodules="$(git -C "$checkout" submodule status --recursive)"; then
        echo "Could not inspect recursive ESP-IDF submodule pins in $checkout." >&2
        return 1
    fi
    if [[ -z "$submodules" ]] || ! awk 'substr($0, 1, 1) != " " { bad = 1; print; } END { exit bad; }' <<<"$submodules"; then
        echo "ESP-IDF submodules are missing or differ from the pinned release commits." >&2
        return 1
    fi

    local dirty
    dirty="$(git -C "$checkout" status --porcelain --untracked-files=all --ignore-submodules=none)"
    if [[ -n "$dirty" ]]; then
        echo "ESP-IDF source has local changes or untracked files: $checkout" >&2
        printf '%s\n' "$dirty" >&2
        return 1
    fi

    if ! git -C "$checkout" submodule foreach --quiet --recursive \
        'test -z "$(git status --porcelain --untracked-files=all)"'; then
        echo "An ESP-IDF recursive submodule has local changes or untracked files." >&2
        return 1
    fi
    echo "Verified ESP-IDF source: $IDF_VERSION ($IDF_COMMIT), all recursive submodules pinned and clean."
}

download_archive() {
    local actual_sha actual_bytes

    if [[ -f "$ARCHIVE_FILE" ]]; then
        actual_sha="$(shasum -a 256 "$ARCHIVE_FILE" | awk '{print $1}')"
        if [[ "$actual_sha" == "$ARCHIVE_SHA256" ]]; then
            echo "Verified cached release archive: $ARCHIVE_FILE"
            return
        fi
        if [[ -e "$ARCHIVE_PART" ]]; then
            echo "Both archive and partial archive exist; refusing to replace either." >&2
            exit 2
        fi
        mv "$ARCHIVE_FILE" "$ARCHIVE_PART"
    fi

    if [[ -f "$ARCHIVE_PART" ]]; then
        actual_bytes="$(wc -c <"$ARCHIVE_PART" | tr -d '[:space:]')"
        if (( actual_bytes >= ARCHIVE_BYTES )); then
            mv "$ARCHIVE_PART" "$ARCHIVE_PART.invalid.$(date -u '+%Y%m%dT%H%M%SZ')"
        fi
    fi

    if [[ -f "$ARCHIVE_PART" ]]; then
        echo "Resuming release archive download at byte $(wc -c <"$ARCHIVE_PART" | tr -d '[:space:]')."
        curl --fail --location --retry 2 --connect-timeout 15 --continue-at - \
            "$ARCHIVE_URL" --output "$ARCHIVE_PART"
    else
        echo "Downloading the pinned ESP-IDF release archive."
        curl --fail --location --retry 2 --connect-timeout 15 \
            "$ARCHIVE_URL" --output "$ARCHIVE_PART"
    fi

    actual_bytes="$(wc -c <"$ARCHIVE_PART" | tr -d '[:space:]')"
    actual_sha="$(shasum -a 256 "$ARCHIVE_PART" | awk '{print $1}')"
    if [[ "$actual_bytes" != "$ARCHIVE_BYTES" || "$actual_sha" != "$ARCHIVE_SHA256" ]]; then
        echo "ESP-IDF release archive verification failed." >&2
        echo "Expected: $ARCHIVE_BYTES bytes, SHA-256 $ARCHIVE_SHA256" >&2
        echo "Found:    $actual_bytes bytes, SHA-256 $actual_sha" >&2
        exit 2
    fi
    mv "$ARCHIVE_PART" "$ARCHIVE_FILE"
    echo "Verified release archive: $ARCHIVE_FILE ($actual_bytes bytes, SHA-256 $actual_sha)"
}

if ! checkout_error "$IDF_DIR"; then
    if [[ -e "$IDF_DIR" ]]; then
        origin="$(git -C "$IDF_DIR" remote get-url origin 2>/dev/null || true)"
        if [[ "$origin" != "$INITIAL_MIRROR_URL" ]]; then
            echo "Refusing to replace an existing ESP-IDF directory from an unknown source: $IDF_DIR" >&2
            exit 2
        fi
        if [[ -e "$INITIAL_GITEE_BACKUP" ]]; then
            echo "Refusing to replace $IDF_DIR because its preservation path already exists: $INITIAL_GITEE_BACKUP" >&2
            exit 2
        fi
        echo "The existing Gitee checkout is incomplete or dirty; it will be preserved at $INITIAL_GITEE_BACKUP."
    fi

    download_archive
    STAGE_DIR="$(mktemp -d "$WORK_DIR/idf-release-extract.XXXXXX")"
    trap 'rm -rf "$STAGE_DIR"' EXIT
    ditto -x -k "$ARCHIVE_FILE" "$STAGE_DIR"
    CANDIDATE="$STAGE_DIR/esp-idf-v5.5.4"
    if ! checkout_error "$CANDIDATE"; then
        echo "The extracted release archive failed source validation." >&2
        exit 2
    fi

    if [[ -e "$IDF_DIR" ]]; then
        mv "$IDF_DIR" "$INITIAL_GITEE_BACKUP"
    fi
    mv "$CANDIDATE" "$IDF_DIR"
    checkout_error "$IDF_DIR"
fi

export IDF_PATH="$IDF_DIR"
export IDF_TOOLS_PATH="$TOOLS_DIR"
export IDF_GITHUB_ASSETS="dl.espressif.cn/github_assets"
export PIP_INDEX_URL="https://mirrors.tuna.tsinghua.edu.cn/pypi/web/simple"
export PIP_CACHE_DIR="$TOOLS_DIR/pip-cache"
export TMPDIR="$TOOLS_DIR/tmp"
export PYTHONDONTWRITEBYTECODE=1
mkdir -p "$PIP_CACHE_DIR" "$TMPDIR"

echo "GitHub release asset mirror: $IDF_GITHUB_ASSETS"
echo "Python package index: $PIP_INDEX_URL"
echo "Python package cache: $PIP_CACHE_DIR"
echo "Temporary directory: $TMPDIR"
echo "Installing ESP32-S3 tools only."
bash "$IDF_DIR/install.sh" esp32s3

echo "Exporting the local ESP-IDF environment for verification."
# shellcheck disable=SC1091
source "$IDF_DIR/export.sh"

[[ "$IDF_PATH" == "$IDF_DIR" ]]
[[ "$IDF_TOOLS_PATH" == "$TOOLS_DIR" ]]
checkout_error "$IDF_DIR"
idf.py --version
python --version
python -m pip freeze
xtensa-esp-elf-gcc --version | head -n 1
cmake --version | head -n 1
ninja --version

echo "Installed ESP-IDF tool inventory:"
python3 "$IDF_DIR/tools/idf_tools.py" list
echo "ESP-IDF $IDF_VERSION setup and export verification completed."
