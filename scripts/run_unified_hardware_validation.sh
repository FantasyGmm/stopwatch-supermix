#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/resolve_esptool_python.sh"
PORT=""
BACKUP=""
BUILD_DIR="$ROOT/build-unified"
OUTPUT_DIR="$ROOT/build-unified/hardware-validation"
DRY_RUN=0

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

export PYTHON="$(resolve_esptool_python "$BUILD_DIR")"

print_python_step() {
    local script="$1"
    shift
    printf 'DRY RUN: %q' "$ROOT/scripts/$script"
    printf ' %q' "$@"
    printf '\n'
}

wait_for_port() {
    local attempt
    for attempt in $(seq 1 30); do
        [[ -e "$PORT" ]] && return 0
        sleep 1
    done
    echo "Watch port did not reappear: $PORT" >&2
    return 1
}

if (( DRY_RUN )); then
    echo "HARDWARE STEP flash-candidate"
    "$ROOT/scripts/flash_unified_candidate.sh" \
        --dry-run --port "$PORT" --backup "$BACKUP" --build-dir "$BUILD_DIR"
    echo "HARDWARE STEP validate-candidate-boot"
    print_python_step validate_watch_boot.py --port "$PORT" \
        --summary "$OUTPUT_DIR/candidate-boot.json" --log "$OUTPUT_DIR/candidate-boot.log"
    echo "HARDWARE STEP mixed-10-cycles"
    print_python_step validate_assistant_graceful_close.py --port "$PORT" \
        --cycles 10 --profiles mixed --warmup --summary "$OUTPUT_DIR/assistant-10.json" \
        --log "$OUTPUT_DIR/assistant-10.log"
    echo "HARDWARE STEP product-smoke"
    print_python_step validate_product_smoke.py --port "$PORT" \
        --summary "$OUTPUT_DIR/product-smoke.json" --log "$OUTPUT_DIR/product-smoke.log"
    echo "HARDWARE STEP mixed-50-cycles"
    print_python_step validate_assistant_graceful_close.py --port "$PORT" \
        --cycles 50 --profiles mixed --summary "$OUTPUT_DIR/assistant-50.json" \
        --log "$OUTPUT_DIR/assistant-50.log"
    echo "HARDWARE STEP full-flash-recovery"
    "$ROOT/scripts/exercise_full_flash_recovery.sh" --dry-run --port "$PORT" \
        --backup "$BACKUP" --build-dir "$BUILD_DIR" \
        --output-dir "$OUTPUT_DIR/recovery"
    echo "HARDWARE STEP final-product-smoke"
    print_python_step validate_product_smoke.py --port "$PORT" \
        --summary "$OUTPUT_DIR/final-product-smoke.json" \
        --log "$OUTPUT_DIR/final-product-smoke.log"
    exit 0
fi

mkdir -p "$OUTPUT_DIR"

echo "HARDWARE STEP flash-candidate"
"$ROOT/scripts/flash_unified_candidate.sh" \
    --port "$PORT" --backup "$BACKUP" --build-dir "$BUILD_DIR"
wait_for_port

echo "HARDWARE STEP validate-candidate-boot"
"$ROOT/scripts/validate_watch_boot.py" --port "$PORT" \
    --summary "$OUTPUT_DIR/candidate-boot.json" --log "$OUTPUT_DIR/candidate-boot.log"

echo "HARDWARE STEP mixed-10-cycles"
"$ROOT/scripts/validate_assistant_graceful_close.py" --port "$PORT" \
    --cycles 10 --profiles mixed --warmup --summary "$OUTPUT_DIR/assistant-10.json" \
    --log "$OUTPUT_DIR/assistant-10.log"

echo "HARDWARE STEP product-smoke"
"$ROOT/scripts/validate_product_smoke.py" --port "$PORT" \
    --summary "$OUTPUT_DIR/product-smoke.json" --log "$OUTPUT_DIR/product-smoke.log"

echo "HARDWARE STEP mixed-50-cycles"
"$ROOT/scripts/validate_assistant_graceful_close.py" --port "$PORT" \
    --cycles 50 --profiles mixed --summary "$OUTPUT_DIR/assistant-50.json" \
    --log "$OUTPUT_DIR/assistant-50.log"

echo "HARDWARE STEP full-flash-recovery"
"$ROOT/scripts/exercise_full_flash_recovery.sh" --port "$PORT" \
    --backup "$BACKUP" --build-dir "$BUILD_DIR" \
    --output-dir "$OUTPUT_DIR/recovery"

echo "HARDWARE STEP final-product-smoke"
"$ROOT/scripts/validate_product_smoke.py" --port "$PORT" \
    --summary "$OUTPUT_DIR/final-product-smoke.json" \
    --log "$OUTPUT_DIR/final-product-smoke.log"

echo "Unified hardware release validation passed. Evidence: $OUTPUT_DIR"
