#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT=""
BACKUP=""
BUILD_DIR="$ROOT/build-unified"
OUTPUT_DIR="$ROOT/build-unified/recovery-evidence"
DRY_RUN=0
RECOVERY_NEEDED=0

usage() {
    echo "Usage: $0 --port PORT --backup FILE [--build-dir DIR] [--output-dir DIR] [--dry-run]" >&2
}

while (( $# > 0 )); do
    case "$1" in
        --port)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            PORT="$2"
            shift 2
            ;;
        --backup)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BACKUP="$2"
            shift 2
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ -z "$PORT" || -z "$BACKUP" ]]; then
    echo "Both --port and --backup are required" >&2
    usage
    exit 2
fi

wait_for_port() {
    local attempt
    for attempt in $(seq 1 30); do
        [[ -e "$PORT" ]] && return 0
        sleep 1
    done
    echo "Watch port did not reappear: $PORT" >&2
    return 1
}

reinstall_candidate_on_failure() {
    local exit_code=$?
    if (( exit_code != 0 && RECOVERY_NEEDED )); then
        echo "Recovery exercise failed after restoring the backup; attempting to reinstall the unified candidate" >&2
        wait_for_port || true
        "$ROOT/scripts/flash_unified_candidate.sh" \
            --port "$PORT" --build-dir "$BUILD_DIR" || true
    fi
    exit "$exit_code"
}

if (( DRY_RUN )); then
    echo "RECOVERY STEP restore-backup"
    "$ROOT/scripts/restore_watch_flash.sh" --dry-run --port "$PORT" --backup "$BACKUP"
    echo "RECOVERY STEP validate-restored-boot"
    printf 'DRY RUN: %q --port %q --summary %q --log %q\n' \
        "$ROOT/scripts/validate_watch_boot.py" "$PORT" \
        "$OUTPUT_DIR/restored-boot.json" "$OUTPUT_DIR/restored-boot.log"
    echo "RECOVERY STEP flash-unified-candidate"
    "$ROOT/scripts/flash_unified_candidate.sh" \
        --dry-run --port "$PORT" --build-dir "$BUILD_DIR"
    echo "RECOVERY STEP validate-unified-boot"
    printf 'DRY RUN: %q --port %q --summary %q --log %q\n' \
        "$ROOT/scripts/validate_watch_boot.py" "$PORT" \
        "$OUTPUT_DIR/unified-boot.json" "$OUTPUT_DIR/unified-boot.log"
    exit 0
fi

mkdir -p "$OUTPUT_DIR"
trap reinstall_candidate_on_failure EXIT

echo "RECOVERY STEP restore-backup"
"$ROOT/scripts/restore_watch_flash.sh" --port "$PORT" --backup "$BACKUP"
RECOVERY_NEEDED=1
wait_for_port

echo "RECOVERY STEP validate-restored-boot"
"$ROOT/scripts/validate_watch_boot.py" \
    --port "$PORT" \
    --summary "$OUTPUT_DIR/restored-boot.json" \
    --log "$OUTPUT_DIR/restored-boot.log"

echo "RECOVERY STEP flash-unified-candidate"
"$ROOT/scripts/flash_unified_candidate.sh" \
    --port "$PORT" --build-dir "$BUILD_DIR"
wait_for_port

echo "RECOVERY STEP validate-unified-boot"
"$ROOT/scripts/validate_watch_boot.py" \
    --port "$PORT" \
    --summary "$OUTPUT_DIR/unified-boot.json" \
    --log "$OUTPUT_DIR/unified-boot.log"

RECOVERY_NEEDED=0
trap - EXIT
echo "Full-flash recovery exercise passed; unified candidate restored"
