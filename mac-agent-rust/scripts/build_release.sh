#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/common.sh"

ARTIFACT_ROOT="${STOPWATCH_ARTIFACT_ROOT:-$PROJECT_ROOT/artifacts/macos-agent}"
LATEST_DIR="$ARTIFACT_ROOT/latest"
SWIFT_MODULE_CACHE_DIR="${STOPWATCH_SWIFT_MODULE_CACHE_DIR:-$FUSION_ROOT/swift-module-cache}"
RELEASE_BUILD_ROOT=""
MOUNT_POINT=""
IMAGE_MOUNTED=0
VERSION_ORIGINAL=""
VERSION_ATTEMPTED=""
FINGERPRINT_ATTEMPTED=""
VERSION_UPDATE_PENDING=0
RELEASE_SIGNING_IDENTITY="${STOPWATCH_CODESIGN_IDENTITY:--}"
RELEASE_SOURCE_ONLY="${STOPWATCH_RELEASE_SOURCE_ONLY:-0}"

run_release_metadata_helper() {
    mkdir -p "$SWIFT_MODULE_CACHE_DIR/clang-scanner" "$SWIFT_MODULE_CACHE_DIR/sdk"
    swift -module-cache-path "$SWIFT_MODULE_CACHE_DIR" \
        -clang-scanner-module-cache-path "$SWIFT_MODULE_CACHE_DIR/clang-scanner" \
        -sdk-module-cache-path "$SWIFT_MODULE_CACHE_DIR/sdk" \
        "$SCRIPT_DIR/release_metadata.swift" "$@"
}
LATEST_ROLLBACK_REQUIRED=0
LATEST_HAD_PRIOR=0
LATEST_PUBLISH_DIR=""
LATEST_PUBLISH_BACKUP=""

release_input_paths() {
    (
        cd "$PROJECT_ROOT"
        {
            find mac-agent-rust -type f \
                ! -path 'mac-agent-rust/target/*' \
                ! -path 'mac-agent-rust/.native/*' \
                ! -path 'mac-agent-rust/.driver-cache/*' \
                ! -name '*.log' \
                ! -path '*/__pycache__/*' -print
            find mac-codex-agent/native -type f \( -name '*.mm' -o -name '*.h' \) -print
            printf '%s\n' LICENSE third_party_notices/stopwatch-hub-usage.md third_party_notices/cc-switch.md \
                mac-codex-agent/install_launch_agent.sh mac-codex-agent/build_release.sh
        } | LC_ALL=C sort
    )
}

release_input_fingerprint() {
    local root="$1"
    shift
    [[ $# -gt 0 ]] || { echo "no release inputs" >&2; return 2; }
    local relative path
    {
        for relative in "$@"; do
            path="$root/$relative"
            [[ -f "$path" && ! -L "$path" ]] || { echo "missing or linked release input: $relative" >&2; return 1; }
            case "$(cd -- "$(dirname -- "$path")" && pwd -P)/$(basename -- "$path")" in
                "$(cd -- "$root" && pwd -P)"/*) ;;
                *) echo "release input escapes root: $relative" >&2; return 1 ;;
            esac
            printf '%s\0' "$relative"
            shasum -a 256 "$path" | awk '{print $1}'
            printf '\0'
        done
    } | shasum -a 256 | awk '{print $1}'
}

release_metadata_field() {
    local metadata="$1"
    local field="$2"
    [[ -f "$metadata" ]] || { printf '\n'; return 0; }
    run_release_metadata_helper get "$metadata" "$field" 2>/dev/null || printf '\n'
}

validate_release_dir() {
    local directory="$1"
    local metadata="$directory/release.json"
    local checksum_path="$directory/SHA256SUMS.txt"
    local version filename fingerprint expected_sha build actual_sha checksum runtime_test_status
    [[ -f "$metadata" && -f "$checksum_path" ]] || { echo "release metadata or checksum file is missing" >&2; return 1; }
    run_release_metadata_helper validate "$metadata" || { echo "invalid release metadata" >&2; return 1; }
    version="$(release_metadata_field "$metadata" version)"
    filename="$(release_metadata_field "$metadata" filename)"
    fingerprint="$(release_metadata_field "$metadata" source_fingerprint)"
    expected_sha="$(release_metadata_field "$metadata" sha256)"
    build="$(release_metadata_field "$metadata" build)"
    runtime_test_status="$(release_metadata_field "$metadata" runtime_self_test)"
    is_release_version "$version" || { echo "invalid release version" >&2; return 1; }
    [[ "$build" == "$((10#${version##*.} + 1))" ]] || { echo "release build number does not match version" >&2; return 1; }
    [[ "$runtime_test_status" == "passed" || "$runtime_test_status" == "skipped" ]] || {
        echo "release metadata has an invalid runtime self-test status" >&2
        return 1
    }
    [[ "$filename" == "StopWatch-Codex-Agent-$version.dmg" ]] || { echo "release filename does not match version" >&2; return 1; }
    [[ "$fingerprint" =~ ^[0-9a-f]{64}$ ]] || { echo "invalid release fingerprint" >&2; return 1; }
    [[ "$expected_sha" =~ ^[0-9a-f]{64}$ ]] || { echo "invalid release checksum" >&2; return 1; }
    [[ -f "$directory/$filename" ]] || { echo "release image is missing" >&2; return 1; }
    actual_sha="$(shasum -a 256 "$directory/$filename" | awk '{print $1}')"
    [[ "$actual_sha" == "$expected_sha" ]] || { echo "release payload checksum mismatch" >&2; return 1; }
    checksum="$(<"$checksum_path")"
    [[ "$checksum" == "$expected_sha  $filename" ]] || { echo "checksum file does not match metadata" >&2; return 1; }
    local expected_filename="$filename"
    local -a actual_files=()
    while IFS= read -r filename; do actual_files+=("$filename"); done < <(find "$directory" -mindepth 1 -maxdepth 1 -exec basename {} \; | LC_ALL=C sort)
    [[ "${#actual_files[@]}" == "3" ]] || { echo "release directory contains stale or unexpected files" >&2; return 1; }
    [[ " ${actual_files[*]} " == *" release.json "* && " ${actual_files[*]} " == *" SHA256SUMS.txt "* && " ${actual_files[*]} " == *" $expected_filename "* ]] || {
        echo "release directory contains stale or unexpected files" >&2
        return 1
    }
}

write_release_metadata() {
    local output="$1" version="$2" fingerprint="$3" filename="$4" sha256="$5" commit="$6" build="$7" runtime_test_status="$8"
    local signing="adhoc"
    [[ "$RELEASE_SIGNING_IDENTITY" == "-" ]] || signing="keychain"
    local built_at
    built_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    run_release_metadata_helper write "$output" \
        "$version" "$fingerprint" "$filename" "$sha256" "$commit" \
        "$build" "$signing" "$built_at" "$runtime_test_status"
}

write_version_atomic() {
    local value="$1"
    local temporary="$VERSION_FILE.release.$$"
    printf '%s\n' "$value" > "$temporary"
    mv -f -- "$temporary" "$VERSION_FILE"
}

detach_image() {
    if [[ "$IMAGE_MOUNTED" == "1" ]]; then
        hdiutil detach -quiet "$MOUNT_POINT" || hdiutil detach -quiet -force "$MOUNT_POINT" || true
        IMAGE_MOUNTED=0
    fi
}

validate_release_image() {
    local image="$1"
    MOUNT_POINT="$RELEASE_BUILD_ROOT/mounted-image"
    mkdir -p "$MOUNT_POINT"
    hdiutil verify -quiet "$image"
    hdiutil attach -quiet -readonly -nobrowse -mountpoint "$MOUNT_POINT" "$image"
    IMAGE_MOUNTED=1
    local mounted_app="$MOUNT_POINT/$APP_NAME"
    [[ -d "$mounted_app" ]] || { echo "DMG is missing $APP_NAME" >&2; return 1; }
    [[ -L "$MOUNT_POINT/Applications" && "$(readlink "$MOUNT_POINT/Applications")" == "/Applications" ]] || {
        echo "DMG Applications shortcut is invalid" >&2
        return 1
    }
    local -a root_entries=()
    while IFS= read -r entry; do root_entries+=("$(basename -- "$entry")"); done < <(find "$MOUNT_POINT" -mindepth 1 -maxdepth 1 -print | LC_ALL=C sort)
    [[ "${#root_entries[@]}" == "2" ]] || { echo "DMG contains unexpected root entries" >&2; return 1; }
    validate_app_contents "$mounted_app"
    detach_image
}

cleanup_release_build() {
    detach_image
    if [[ "$LATEST_ROLLBACK_REQUIRED" == "1" ]]; then rollback_latest_publish || true; fi
    if [[ "$VERSION_UPDATE_PENDING" == "1" ]]; then
        local published_version published_fingerprint
        published_version="$(release_metadata_field "$LATEST_DIR/release.json" version)"
        published_fingerprint="$(release_metadata_field "$LATEST_DIR/release.json" source_fingerprint)"
        if [[ "$published_version" == "$VERSION_ATTEMPTED" && "$published_fingerprint" == "$FINGERPRINT_ATTEMPTED" ]]; then
            VERSION_UPDATE_PENDING=0
        else
            write_version_atomic "$VERSION_ORIGINAL" || true
        fi
    fi
    [[ -z "$RELEASE_BUILD_ROOT" || ! -d "$RELEASE_BUILD_ROOT" ]] || rm -rf -- "$RELEASE_BUILD_ROOT"
}

release_usage() {
    cat <<'EOF'
Usage: build_release.sh [--dest DIRECTORY | --prefix DIRECTORY] [--sign-identity ID]

Set STOPWATCH_SKIP_RUNTIME_TESTS=1 to skip the packaged runtime self-test.
The default is 0, which runs the self-test during staged and published validation.

Builds and validates a universal2 DMG in <destination>/latest. The default
destination is ../artifacts/macos-agent. The installer is invoked in isolated
build-only mode; no app is installed into the user's Applications directory.
EOF
}

main() {
    local -a original_args=("$@")
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --dest|--prefix)
                [[ $# -ge 2 ]] || { release_usage >&2; return 2; }
                ARTIFACT_ROOT="$2"
                LATEST_DIR="$ARTIFACT_ROOT/latest"
                shift 2
                ;;
            --sign-identity)
                [[ $# -ge 2 ]] || { release_usage >&2; return 2; }
                RELEASE_SIGNING_IDENTITY="$2"
                shift 2
                ;;
            --help|-h) release_usage; return 0 ;;
            *) echo "Unknown release option: $1" >&2; release_usage >&2; return 2 ;;
        esac
    done
    validate_runtime_test_setting || return
    [[ "$(uname -s)" == "Darwin" ]] || { echo "The macOS release must be built on macOS" >&2; return 1; }
    mkdir -p "$ARTIFACT_ROOT"
    if [[ "${STOPWATCH_RELEASE_LOCK_HELD:-}" != "$ARTIFACT_ROOT/.release-build.lock" ]]; then
        command -v lockf >/dev/null 2>&1 || { echo "macOS lockf is required for release publication" >&2; return 1; }
        exec lockf -s -t 300 "$ARTIFACT_ROOT/.release-build.lock" \
            /usr/bin/env "STOPWATCH_RELEASE_LOCK_HELD=$ARTIFACT_ROOT/.release-build.lock" \
            "$BASH" "$SCRIPT_DIR/build_release.sh" "${original_args[@]}"
    fi
    local current_version
    current_version="$(<"$VERSION_FILE")"
    is_release_version "$current_version" || { echo "Invalid Agent version: $current_version" >&2; return 2; }
    local -a release_inputs=()
    local input
    while IFS= read -r input; do release_inputs+=("$input"); done < <(release_input_paths)
    local fingerprint previous_fingerprint version patch build_number
    fingerprint="$(release_input_fingerprint "$PROJECT_ROOT" "${release_inputs[@]}")"
    previous_fingerprint="$(release_metadata_field "$LATEST_DIR/release.json" source_fingerprint)"
    version="$(next_release_version "$current_version" "$previous_fingerprint" "$fingerprint")"
    patch="${version##*.}"
    build_number="$((10#$patch + 1))"
    if [[ -n "$previous_fingerprint" && "$previous_fingerprint" == "$fingerprint" ]] && validate_release_dir "$LATEST_DIR"; then
        echo "Latest Agent release is already $version"
        return 0
    fi

    RELEASE_BUILD_ROOT="$(mktemp -d "$ARTIFACT_ROOT/.release-build.XXXXXX")"
    VERSION_ORIGINAL="$current_version"
    VERSION_ATTEMPTED="$version"
    FINGERPRINT_ATTEMPTED="$fingerprint"
    trap cleanup_release_build EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM

    if [[ "$version" != "$current_version" ]]; then
        VERSION_UPDATE_PENDING=1
        write_version_atomic "$version"
    fi

    local app_parent="$RELEASE_BUILD_ROOT/app"
    local app="$app_parent/$APP_NAME"
    mkdir -p "$app_parent"
    STOPWATCH_AGENT_VERSION="$version" \
        STOPWATCH_AGENT_BUILD_NUMBER="$build_number" \
        STOPWATCH_CODESIGN_IDENTITY="$RELEASE_SIGNING_IDENTITY" \
        STOPWATCH_SKIP_RUNTIME_TESTS="${STOPWATCH_SKIP_RUNTIME_TESTS:-0}" \
        "$SCRIPT_DIR/install.sh" --prefix "$app_parent"

    local payload="$RELEASE_BUILD_ROOT/payload"
    local staging="$RELEASE_BUILD_ROOT/latest"
    local filename="StopWatch-Codex-Agent-$version.dmg"
    mkdir -p "$payload" "$staging"
    ditto "$app" "$payload/$APP_NAME"
    ln -s /Applications "$payload/Applications"
    hdiutil create -quiet -volname "StopWatch Codex Agent" \
        -srcfolder "$payload" -ov -format UDZO "$staging/$filename"
    validate_release_image "$staging/$filename"

    local sha256 commit
    sha256="$(shasum -a 256 "$staging/$filename" | awk '{print $1}')"
    printf '%s  %s\n' "$sha256" "$filename" > "$staging/SHA256SUMS.txt"
    commit="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
    write_release_metadata "$staging/release.json" "$version" "$fingerprint" \
        "$filename" "$sha256" "$commit" "$build_number" \
        "$(runtime_self_test_status)"
    validate_release_dir "$staging"
    publish_latest_release "$staging" "$LATEST_DIR"
    VERSION_UPDATE_PENDING=0

    echo "Built $LATEST_DIR/$filename"
    RELEASE_BUILD_ROOT=""
    rm -rf -- "$(dirname -- "$staging")"
    trap - EXIT HUP INT TERM
}

if [[ "${BASH_SOURCE[0]}" == "$0" && "$RELEASE_SOURCE_ONLY" != "1" ]]; then
    main "$@"
fi

