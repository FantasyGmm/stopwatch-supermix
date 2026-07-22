#!/usr/bin/env bash

resolve_esptool_python() {
    local build_dir="${1:-}"
    local cache candidate

    if [[ -n "${PYTHON:-}" ]]; then
        printf '%s\n' "$PYTHON"
        return 0
    fi

    cache="$build_dir/CMakeCache.txt"
    if [[ -f "$cache" ]]; then
        candidate="$(awk -F= '$1 == "PYTHON:UNINITIALIZED" { print substr($0, index($0, "=") + 1); exit }' "$cache")"
        if [[ -n "$candidate" && -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    fi

    for candidate in python3 python; do
        if command -v "$candidate" >/dev/null 2>&1 && \
                "$candidate" -c 'import esptool' >/dev/null 2>&1; then
            command -v "$candidate"
            return 0
        fi
    done

    echo "Unable to find the ESP-IDF Python interpreter; set PYTHON or build the unified candidate first" >&2
    return 1
}
