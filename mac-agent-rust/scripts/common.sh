#!/bin/bash

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
CRATE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd -P)"
PROJECT_ROOT="$(cd -- "$CRATE_DIR/.." && pwd -P)"
FUSION_ROOT="$(cd -- "$PROJECT_ROOT/.." && pwd -P)"
LEGACY_ROOT="$PROJECT_ROOT/mac-codex-agent"
VERSION_FILE="$LEGACY_ROOT/VERSION"
INFO_TEMPLATE="$CRATE_DIR/resources/Info.plist.template"
APP_NAME="StopWatch Codex Agent.app"
APP_EXECUTABLE="StopWatchCodexAgent"
APP_BUNDLE_ID="com.stopwatch.codex-agent"
CODE_REQUIREMENT='=designated => identifier "com.stopwatch.codex-agent"'
PUBLISH_ROLLBACK_REQUIRED=0
PUBLISH_HAD_PRIOR=0
PUBLISH_APP_DIR=""
PUBLISH_BACKUP_APP=""
LATEST_ROLLBACK_REQUIRED=0
LATEST_HAD_PRIOR=0
LATEST_PUBLISH_DIR=""
LATEST_PUBLISH_BACKUP=""

is_release_version() {
    [[ "$1" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]
}

next_release_version() {
    local current="$1"
    local previous_fingerprint="$2"
    local fingerprint="$3"
    is_release_version "$current" || { echo "Invalid Agent version: $current" >&2; return 2; }
    if [[ -z "$previous_fingerprint" || "$previous_fingerprint" == "$fingerprint" ]]; then
        printf '%s\n' "$current"
        return 0
    fi
    local major minor patch
    IFS=. read -r major minor patch <<<"$current"
    printf '%s.%s.%s\n' "$major" "$minor" "$((10#$patch + 1))"
}

write_info_plist() {
    local output="$1"
    local version="${2:-${AGENT_VERSION:-$(<"$VERSION_FILE")}}"
    local build_number="${3:-${AGENT_BUILD_NUMBER:-$((10#${version##*.} + 1))}}"
    local temporary="$output.tmp.$$"
    sed -e "s/@VERSION@/$version/g" -e "s/@BUILD_NUMBER@/$build_number/g" \
        "$INFO_TEMPLATE" > "$temporary"
    plutil -lint "$temporary" >/dev/null
    mv -f "$temporary" "$output"
}

validate_no_key_material() {
    local app_path="$1"
    if find "$app_path" -type f \( -iname '*voice*key*.h' -o -iname '*device*key*.h' \
        -o -iname '*provision*' -o -name '*.key' \) -print -quit | grep -q .; then
        echo "Forbidden key or provisioning file in app bundle" >&2
        return 1
    fi
    if grep -R -I -q -x -E -- '-----BEGIN [A-Z ]*PRIVATE KEY-----' "$app_path"; then
        echo "Possible key material in app bundle" >&2
        return 1
    fi
    if grep -R -I -q -E '(^|[^0-9A-Za-z+/])[0-9A-Fa-f]{32}([^0-9A-Za-z+/]|$)' \
        "$app_path/Contents/Resources" "$app_path/Contents/MacOS" "$app_path/Contents/Info.plist"; then
        echo "Possible key material in app bundle" >&2
        return 1
    fi
}

validate_app_paths() {
    local app_path="$1"
    local plist="$app_path/Contents/Info.plist"
    if /usr/libexec/PlistBuddy -c 'Print :LSEnvironment' "$plist" >/dev/null 2>&1; then
        echo "Info.plist must not contain path-bound LSEnvironment values" >&2
        return 1
    fi
    if grep -F "$PROJECT_ROOT" "$plist" >/dev/null || {
        [[ -n "${HOME:-}" ]] && grep -F "$HOME" "$plist" >/dev/null
    }; then
        echo "Info.plist contains a checkout or home path" >&2
        return 1
    fi
    local python_path=""
    python_path="$(find "$app_path" \( -iname '*.py' -o -iname '*.pyc' -o -iname '*.pyo' \
        -o -iname 'python' -o -iname 'python[0-9]*' -o -iname 'Python*.framework' \) -print -quit)"
    if [[ -n "$python_path" ]]; then
        echo "Unexpected Python runtime or source in app bundle: $python_path" >&2
        return 1
    fi
}

validate_universal_macho_tree() {
    local app_path="$1"
    local candidate=""
    local architectures=""
    while IFS= read -r -d '' candidate; do
        if ! file -b "$candidate" | grep -q 'Mach-O'; then
            continue
        fi
        architectures="$(lipo -archs "$candidate")"
        if [[ " $architectures " != *" arm64 "* ]] ||
            [[ " $architectures " != *" x86_64 "* ]]; then
            echo "Non-universal Mach-O: $candidate ($architectures)" >&2
            return 1
        fi
    done < <(find "$app_path" -type f -print0)
}

validate_runtime_test_setting() {
    case "${STOPWATCH_SKIP_RUNTIME_TESTS:-0}" in
        0|1) ;;
        *)
            echo "STOPWATCH_SKIP_RUNTIME_TESTS must be 0 or 1" >&2
            return 2
            ;;
    esac
}

runtime_self_test_status() {
    if [[ "${STOPWATCH_SKIP_RUNTIME_TESTS:-0}" == "1" ]]; then
        printf 'skipped\n'
    else
        printf 'passed\n'
    fi
}

smoke_test_packaged_runtime() {
    local app_path="$1"
    local executable="$app_path/Contents/MacOS/$APP_EXECUTABLE"
    validate_runtime_test_setting || return
    if [[ "${STOPWATCH_SKIP_RUNTIME_TESTS:-0}" == "1" ]]; then
        return 0
    fi
    env -i PATH="/usr/bin:/bin" TMPDIR="${TMPDIR:-/tmp}" \
        "$executable" --self-test
}

validate_app_contents() {
    local app_path="$1"
    local resources="$app_path/Contents/Resources"
    local required_path=""
    for required_path in \
        "$app_path/Contents/MacOS/$APP_EXECUTABLE" \
        "$app_path/Contents/Info.plist" \
        "$resources/model-pricing.json" \
        "$resources/LICENSE" \
        "$resources/third-party-licenses/stopwatch-hub-usage.md" \
        "$resources/third-party-licenses/cc-switch.md" \
        "$resources/THIRD-PARTY-NOTICES.txt"; do
        if [[ ! -f "$required_path" ]]; then
            echo "Missing packaged runtime file: $required_path" >&2
            return 1
        fi
    done
    if [[ ! -d "$resources/third-party-licenses" ]]; then
        echo "Missing packaged third-party license texts" >&2
        return 1
    fi
    if ! find "$resources/third-party-licenses" -type f -print -quit | grep -q .; then
        echo "No third-party license texts were packaged" >&2
        return 1
    fi
    if ! file -b "$app_path/Contents/MacOS/$APP_EXECUTABLE" | grep -q 'Mach-O'; then
        echo "Packaged runtime is not a Mach-O executable" >&2
        return 1
    fi
    if [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app_path/Contents/Info.plist")" != "$APP_BUNDLE_ID" ]]; then
        echo "Unexpected bundle identifier" >&2
        return 1
    fi
    if [[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleName' "$app_path/Contents/Info.plist")" != "StopWatch Codex Agent" ]]; then
        echo "Unexpected bundle name" >&2
        return 1
    fi
    validate_app_paths "$app_path" || return
    validate_no_key_material "$app_path" || return
    validate_universal_macho_tree "$app_path" || return
    smoke_test_packaged_runtime "$app_path" || return
    codesign --verify --deep --strict --verbose=2 "$app_path" || return
}

validate_staged_app() {
    validate_app_contents "$1"
}

validate_published_app() {
    validate_app_contents "$1"
}

rollback_publish() {
    [[ "$PUBLISH_ROLLBACK_REQUIRED" == "1" ]] || return 0
    if [[ "$PUBLISH_HAD_PRIOR" == "1" ]]; then
        if [[ -e "$PUBLISH_BACKUP_APP" ]]; then
            rm -rf -- "$PUBLISH_APP_DIR"
            mv -- "$PUBLISH_BACKUP_APP" "$PUBLISH_APP_DIR" || {
                echo "Could not restore prior app from $PUBLISH_BACKUP_APP" >&2
                return 1
            }
        elif [[ ! -e "$PUBLISH_APP_DIR" ]]; then
            echo "Prior app is missing from both publish and backup paths" >&2
            return 1
        fi
    else
        rm -rf -- "$PUBLISH_APP_DIR"
    fi
    PUBLISH_ROLLBACK_REQUIRED=0
    PUBLISH_HAD_PRIOR=0
}

publish_validated_app() {
    local staging_app="$1"
    local app_dir="$2"
    local backup_root="$3"
    local backup_app="$backup_root/$APP_NAME"
    validate_staged_app "$staging_app" || return
    mkdir -p "$backup_root"
    PUBLISH_APP_DIR="$app_dir"
    PUBLISH_BACKUP_APP="$backup_app"
    PUBLISH_HAD_PRIOR=0
    PUBLISH_ROLLBACK_REQUIRED=0
    if [[ -e "$app_dir" ]]; then
        PUBLISH_HAD_PRIOR=1
        PUBLISH_ROLLBACK_REQUIRED=1
        if ! mv -- "$app_dir" "$backup_app"; then
            PUBLISH_HAD_PRIOR=0
            PUBLISH_ROLLBACK_REQUIRED=0
            return 1
        fi
    fi
    PUBLISH_ROLLBACK_REQUIRED=1
    if ! mv -- "$staging_app" "$app_dir"; then
        rollback_publish || return 1
        rm -rf -- "$backup_root"
        return 1
    fi
    PUBLISH_ROLLBACK_REQUIRED=1
    if ! validate_published_app "$app_dir"; then
        rollback_publish || return 1
        rm -rf -- "$backup_root"
        return 1
    fi
    PUBLISH_ROLLBACK_REQUIRED=0
    PUBLISH_HAD_PRIOR=0
    rm -rf -- "$backup_root"
}

rollback_latest_publish() {
    [[ "$LATEST_ROLLBACK_REQUIRED" == "1" ]] || return 0
    if [[ "$LATEST_HAD_PRIOR" == "1" ]]; then
        if [[ -e "$LATEST_PUBLISH_BACKUP" ]]; then
            rm -rf -- "$LATEST_PUBLISH_DIR"
            mv -- "$LATEST_PUBLISH_BACKUP" "$LATEST_PUBLISH_DIR" || {
                echo "Could not restore prior release from $LATEST_PUBLISH_BACKUP" >&2
                return 1
            }
        elif [[ ! -e "$LATEST_PUBLISH_DIR" ]]; then
            echo "Prior release is missing from both publish and backup paths" >&2
            return 1
        fi
    else
        rm -rf -- "$LATEST_PUBLISH_DIR"
    fi
    LATEST_ROLLBACK_REQUIRED=0
    LATEST_HAD_PRIOR=0
}

publish_latest_release() {
    local staging="$1"
    local latest="$2"
    local parent="$(dirname -- "$latest")"
    local backup="$parent/.latest-backup.$$"
    [[ "$(basename -- "$latest")" == "latest" ]] || { echo "Release destination must be named latest" >&2; return 2; }
    validate_release_dir "$staging" || return
    mkdir -p "$parent"
    [[ ! -e "$backup" ]] || { echo "Release backup already exists: $backup" >&2; return 2; }
    LATEST_PUBLISH_DIR="$latest"
    LATEST_PUBLISH_BACKUP="$backup"
    LATEST_HAD_PRIOR=0
    LATEST_ROLLBACK_REQUIRED=1
    if [[ -e "$latest" ]]; then
        LATEST_HAD_PRIOR=1
        if ! mv -- "$latest" "$backup"; then
            LATEST_HAD_PRIOR=0
            LATEST_ROLLBACK_REQUIRED=0
            return 1
        fi
    fi
    if ! mv -- "$staging" "$latest"; then
        rollback_latest_publish
        return 1
    fi
    if ! validate_release_dir "$latest"; then
        rollback_latest_publish
        return 1
    fi
    LATEST_ROLLBACK_REQUIRED=0
    LATEST_HAD_PRIOR=0
    if [[ -e "$backup" ]]; then rm -rf -- "$backup"; fi
}

