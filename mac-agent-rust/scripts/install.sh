#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/common.sh"

BUILD_ROOT=""
BACKUP_ROOT=""
STAGING_APP=""
OUTPUT_DEST=""
OUTPUT_PARENT=""
LOG_DIR=""
AGENT_VERSION="${STOPWATCH_AGENT_VERSION:-}"
if [[ -z "$AGENT_VERSION" ]]; then IFS= read -r AGENT_VERSION < "$VERSION_FILE"; fi
AGENT_BUILD_NUMBER="${STOPWATCH_AGENT_BUILD_NUMBER:-}"
SIGNING_IDENTITY="${STOPWATCH_CODESIGN_IDENTITY:--}"
INSTALLER_SOURCE_ONLY="${STOPWATCH_INSTALLER_SOURCE_ONLY:-0}"

target_is_installed() {
    local target="$1"
    rustup target list --installed | grep -Fxq "$target"
}

use_project_rustup_home_for_x86() {
    local target="x86_64-apple-darwin"
    target_is_installed "$target" && return 0
    command -v rustup >/dev/null 2>&1 || { echo "rustup is required to provision the isolated x86_64 standard library" >&2; return 1; }

    local source_home active_toolchain source_toolchain project_home
    source_home="$(rustup show home)"
    active_toolchain="$(rustup show active-toolchain | awk '{print $1}')"
    source_toolchain="$source_home/toolchains/$active_toolchain"
    project_home="$FUSION_ROOT/rustup-home"
    if [[ ! -d "$source_toolchain" ]]; then
        echo "Active Rust toolchain is missing: $source_toolchain" >&2
        return 1
    fi
    if [[ ! -x "$project_home/toolchains/$active_toolchain/bin/rustc" ]]; then
        rm -rf -- "$project_home/toolchains/$active_toolchain"
        mkdir -p "$project_home/toolchains"
        cp -cR "$source_toolchain" "$project_home/toolchains/$active_toolchain"
        if [[ -f "$source_home/settings.toml" && ! -f "$project_home/settings.toml" ]]; then
            cp "$source_home/settings.toml" "$project_home/settings.toml"
        fi
        if [[ -d "$source_home/update-hashes" && ! -d "$project_home/update-hashes" ]]; then
            cp -cR "$source_home/update-hashes" "$project_home/update-hashes"
        fi
    fi

    export RUSTUP_HOME="$project_home"
    export RUSTUP_TOOLCHAIN="$active_toolchain"
    rustup target add --toolchain "$active_toolchain" "$target"
    target_is_installed "$target" || { echo "x86_64 Rust standard library was not installed in the project toolchain" >&2; return 1; }
}

build_universal_binary() {
    local output="$1"
    local cargo_home="${CARGO_HOME:-$FUSION_ROOT/cargo-home}"
    local target_dir="${CARGO_TARGET_DIR:-$FUSION_ROOT/rust-target}"
    local cargo_binary="${CARGO:-cargo}"
    export CARGO_HOME="$cargo_home"
    export CARGO_TARGET_DIR="$target_dir"
    command -v xcrun >/dev/null 2>&1 || { echo "Xcode command line tools are required" >&2; return 1; }
    [[ "$(uname -s)" == "Darwin" ]] || { echo "The macOS app package must be built on macOS" >&2; return 1; }
    use_project_rustup_home_for_x86 || return
    (
        cd "$CRATE_DIR"
        "$cargo_binary" build --release --locked --features native-runtime --target aarch64-apple-darwin
        "$cargo_binary" build --release --locked --features native-runtime --target x86_64-apple-darwin
    )
    lipo -create \
        "$target_dir/aarch64-apple-darwin/release/stopwatch-codex-agent" \
        "$target_dir/x86_64-apple-darwin/release/stopwatch-codex-agent" \
        -output "$output"
    chmod 755 "$output"
    local architectures
    architectures="$(lipo -archs "$output")"
    [[ " $architectures " == *" arm64 "* && " $architectures " == *" x86_64 "* ]] || {
        echo "Universal2 executable is missing an architecture: $architectures" >&2
        return 1
    }
}

sign_staged_app() {
    local app_path="$1"
    codesign --force --deep --sign "$SIGNING_IDENTITY" --requirements "$CODE_REQUIREMENT" "$app_path"
    codesign --verify --deep --strict --verbose=2 "$app_path"
}

copy_upstream_notices() {
    local resources="$1"
    [[ -f "$PROJECT_ROOT/LICENSE" ]] || { echo "Project LICENSE is missing" >&2; return 1; }
    [[ -f "$PROJECT_ROOT/third_party_notices/stopwatch-hub-usage.md" ]] || {
        echo "StopWatch Hub usage notice is missing" >&2
        return 1
    }
    [[ -f "$PROJECT_ROOT/third_party_notices/cc-switch.md" ]] || {
        echo "CC Switch compatibility notice is missing" >&2
        return 1
    }
    cp "$PROJECT_ROOT/LICENSE" "$resources/LICENSE"
    mkdir -p "$resources/third-party-licenses"
    cp "$PROJECT_ROOT/third_party_notices/stopwatch-hub-usage.md" \
        "$resources/third-party-licenses/stopwatch-hub-usage.md"
    cp "$PROJECT_ROOT/third_party_notices/cc-switch.md" \
        "$resources/third-party-licenses/cc-switch.md"
}

build_staged_app() {
    local app_path="$1"
    local version="$2"
    local build_number="$3"
    local contents="$app_path/Contents"
    local resources="$contents/Resources"
    mkdir -p "$contents/MacOS" "$resources"
    build_universal_binary "$contents/MacOS/$APP_EXECUTABLE"
    cp "$CRATE_DIR/model-pricing.json" "$resources/model-pricing.json"
    "$SCRIPT_DIR/collect_licenses.sh" "$CRATE_DIR/Cargo.lock" "$resources"
    copy_upstream_notices "$resources"
    write_info_plist "$contents/Info.plist" "$version" "$build_number"
    sign_staged_app "$app_path"
    local embedded_version
    if [[ "${STOPWATCH_SKIP_RUNTIME_TESTS:-0}" == "1" ]]; then
        embedded_version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$contents/Info.plist")"
        [[ "$embedded_version" == "$version" ]] || {
            echo "Bundle version $embedded_version does not match requested version $version" >&2
            return 1
        }
        local embedded_build
        embedded_build="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$contents/Info.plist")"
        [[ "$embedded_build" == "$build_number" ]] || {
            echo "Bundle build $embedded_build does not match requested build $build_number" >&2
            return 1
        }
    else
        embedded_version="$("$contents/MacOS/$APP_EXECUTABLE" --version)"
        [[ "$embedded_version" == "$version" ]] || {
            echo "Binary version $embedded_version does not match bundle version $version" >&2
            return 1
        }
    fi
    validate_staged_app "$app_path"
}

cleanup_install() {
    if [[ "$PUBLISH_ROLLBACK_REQUIRED" == "1" ]]; then rollback_publish || true; fi
    [[ -z "$BUILD_ROOT" ]] || rm -rf -- "$BUILD_ROOT"
    if [[ -n "$BACKUP_ROOT" && "$PUBLISH_ROLLBACK_REQUIRED" != "1" ]]; then rm -rf -- "$BACKUP_ROOT"; fi
}

handle_signal() {
    exit "$1"
}

installer_usage() {
    cat <<'EOF'
Usage: install_launch_agent.sh [--install | --build-only] [--dest APP.app | --prefix DIRECTORY] [--sign-identity ID]

Set STOPWATCH_SKIP_RUNTIME_TESTS=1 to skip the packaged runtime self-test.
The default is 0, which runs the self-test during staged and published validation.

With no options, builds and atomically installs StopWatch Codex Agent.app in
~/Applications. It never launches the app or creates a login item. --dest and
--prefix select an isolated output and imply build-only mode; --dest names the
app bundle, while --prefix is the directory that will contain the app bundle.
EOF
}

main() {
    local -a original_args=("$@")
    local mode="install" prefix="" destination="" explicit_install=0
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --install) mode="install"; explicit_install=1; shift ;;
            --build-only) mode="build-only"; shift ;;
            --dest|--prefix|--sign-identity)
                [[ $# -ge 2 ]] || { installer_usage >&2; return 2; }
                case "$1" in
                    --dest) destination="$2" ;;
                    --prefix) prefix="$2" ;;
                    --sign-identity) SIGNING_IDENTITY="$2" ;;
                esac
                shift 2
                ;;
            --help|-h) installer_usage; return 0 ;;
            *) echo "Unknown installer option: $1" >&2; installer_usage >&2; return 2 ;;
        esac
    done
    if [[ -n "$prefix" && -n "$destination" ]]; then echo "Use only one of --dest or --prefix" >&2; return 2; fi
    if [[ "$explicit_install" == "1" && ( -n "$prefix" || -n "$destination" ) ]]; then
        echo "--install cannot be combined with an isolated destination" >&2
        return 2
    fi
    if [[ -n "$prefix" || -n "$destination" ]]; then mode="build-only"; fi
    if [[ "$mode" == "build-only" && -z "$prefix" && -z "$destination" ]]; then
        echo "--build-only requires --dest or --prefix" >&2
        return 2
    fi
    validate_runtime_test_setting || return
    is_release_version "$AGENT_VERSION" || { echo "Invalid Agent version: $AGENT_VERSION" >&2; return 2; }
    if [[ -z "$AGENT_BUILD_NUMBER" ]]; then AGENT_BUILD_NUMBER="$((10#${AGENT_VERSION##*.} + 1))"; fi
    [[ "$AGENT_BUILD_NUMBER" =~ ^[1-9][0-9]*$ ]] || { echo "Invalid Agent build number: $AGENT_BUILD_NUMBER" >&2; return 2; }

    if [[ -n "$destination" ]]; then
        [[ "$(basename -- "$destination")" == "$APP_NAME" ]] || { echo "--dest must name $APP_NAME" >&2; return 2; }
        OUTPUT_DEST="$destination"
        OUTPUT_PARENT="$(dirname -- "$destination")"
    elif [[ -n "$prefix" ]]; then
        OUTPUT_PARENT="$prefix"
        OUTPUT_DEST="$OUTPUT_PARENT/$APP_NAME"
    else
        local home_path="${HOME:-}"
        [[ -n "$home_path" ]] || { echo "HOME is required for the default install destination" >&2; return 2; }
        OUTPUT_PARENT="${STOPWATCH_APP_PARENT:-$home_path/Applications}"
        OUTPUT_DEST="$OUTPUT_PARENT/$APP_NAME"
    fi
    PUBLISH_LOCK="$OUTPUT_PARENT/.stopwatch-agent-publish.lock"
    LOG_DIR="${HOME:-}/Library/Logs"
    mkdir -p "$OUTPUT_PARENT"
    if [[ "${STOPWATCH_APP_PUBLISH_LOCK_HELD:-}" != "$PUBLISH_LOCK" ]]; then
        command -v lockf >/dev/null 2>&1 || { echo "macOS lockf is required for app publication" >&2; return 1; }
        exec lockf -s -t 300 "$PUBLISH_LOCK" \
            /usr/bin/env "STOPWATCH_APP_PUBLISH_LOCK_HELD=$PUBLISH_LOCK" \
            "$BASH" "$SCRIPT_DIR/install.sh" "${original_args[@]}"
    fi
    trap cleanup_install EXIT
    trap 'handle_signal 129' HUP
    trap 'handle_signal 130' INT
    trap 'handle_signal 143' TERM
    BUILD_ROOT="$(mktemp -d "$OUTPUT_PARENT/.stopwatch-agent-build.XXXXXX")"
    STAGING_APP="$BUILD_ROOT/$APP_NAME"
    BACKUP_ROOT="$(mktemp -d "$OUTPUT_PARENT/.stopwatch-agent-backup.XXXXXX")"

    build_staged_app "$STAGING_APP" "$AGENT_VERSION" "$AGENT_BUILD_NUMBER"
    publish_validated_app "$STAGING_APP" "$OUTPUT_DEST" "$BACKUP_ROOT"
    BACKUP_ROOT=""

    if [[ "$mode" == "install" ]]; then
        mkdir -p "$LOG_DIR"
        touch "$LOG_DIR/stopwatch-codex-agent.log"
        echo "Installed $OUTPUT_DEST"
        echo "Open StopWatch Codex Agent manually from your Applications folder."
        echo "Log: $LOG_DIR/stopwatch-codex-agent.log"
    else
        echo "Built and validated $OUTPUT_DEST"
    fi
    if [[ "${STOPWATCH_SKIP_RUNTIME_TESTS:-0}" == "1" ]]; then
        echo "Packaged runtime self-test: skipped (STOPWATCH_SKIP_RUNTIME_TESTS=1)"
    fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" && "$INSTALLER_SOURCE_ONLY" != "1" ]]; then
    main "$@"
fi

