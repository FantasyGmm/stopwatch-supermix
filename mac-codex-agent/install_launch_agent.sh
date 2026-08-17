#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LABEL="com.stopwatch.codex-agent"
CODE_REQUIREMENT='=designated => identifier "com.stopwatch.codex-agent"'
LOG_DIR="$HOME/Library/Logs"
APP_PARENT="$HOME/Applications"
APP_DIR="$APP_PARENT/StopWatch Codex Agent.app"
PUBLISH_LOCK="$APP_PARENT/.stopwatch-agent-publish.lock"
NATIVE_BUILD_DIR="$ROOT_DIR/.native"
NATIVE_LIBRARY="$NATIVE_BUILD_DIR/libstopwatch_voice_audio.dylib"
DRIVER_CACHE="$ROOT_DIR/.driver-cache"
DRIVER_PACKAGE_NAME="BlackHole2ch-0.7.1.pkg"
DRIVER_PACKAGE="$DRIVER_CACHE/BlackHole2ch-0.7.1.pkg"
DRIVER_LOCK="$DRIVER_CACHE/.blackhole-package.lock"
DRIVER_URL="https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg"
BLACKHOLE_SHA256="57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf"

DRIVER_TEMP=""
DRIVER_LOCK_OWNED=0
BUILD_ROOT=""
BACKUP_ROOT=""
PUBLISH_APP_DIR=""
PUBLISH_BACKUP_APP=""
PUBLISH_HAD_PRIOR=0
PUBLISH_ROLLBACK_REQUIRED=0
PUBLISH_LOCK_OWNED=0

# Production voice keys are created and rotated by the watch registry at
# runtime. Packaging never receives, generates, logs, or bundles key material.

verify_driver_package() {
    local package_path="$1"
    printf '%s  %s\n' "$BLACKHOLE_SHA256" "$package_path" |
        shasum -a 256 --check
}

release_driver_lock() {
    if [[ "${DRIVER_LOCK_OWNED:-0}" != "1" ]]; then
        return
    fi

    local owner=""
    if [[ -f "$DRIVER_LOCK/owner" ]]; then
        owner="$(<"$DRIVER_LOCK/owner")"
    fi
    if [[ "$owner" == "$$" ]]; then
        rm -rf "$DRIVER_LOCK"
    fi
    DRIVER_LOCK_OWNED=0
}

cleanup_driver_cache_state() {
    if [[ -n "${DRIVER_TEMP:-}" ]]; then
        rm -f "$DRIVER_TEMP"
        DRIVER_TEMP=""
    fi
    release_driver_lock
}

acquire_driver_lock() {
    mkdir -p "$DRIVER_CACHE"
    local attempts=0
    local owner=""
    while ! mkdir "$DRIVER_LOCK" 2>/dev/null; do
        owner=""
        if [[ -f "$DRIVER_LOCK/owner" ]]; then
            owner="$(<"$DRIVER_LOCK/owner")"
        fi
        if [[ -n "$owner" ]] && ! kill -0 "$owner" 2>/dev/null; then
            rm -rf "$DRIVER_LOCK"
            continue
        fi
        attempts=$((attempts + 1))
        if [[ -z "$owner" ]] && (( attempts % 20 == 0 )); then
            rm -rf "$DRIVER_LOCK"
            continue
        fi
        if (( attempts >= 300 )); then
            echo "Timed out waiting for driver cache lock" >&2
            return 1
        fi
        sleep 0.1
    done

    printf '%s\n' "$$" > "$DRIVER_LOCK/owner"
    DRIVER_LOCK_OWNED=1
}

ensure_driver_package() {
    acquire_driver_lock || return

    if [[ -f "$DRIVER_PACKAGE" ]] && verify_driver_package "$DRIVER_PACKAGE"; then
        release_driver_lock
        return
    fi

    if [[ -e "$DRIVER_PACKAGE" ]]; then
        rm -f "$DRIVER_PACKAGE"
    fi

    if ! DRIVER_TEMP="$(mktemp "$DRIVER_CACHE/.${DRIVER_PACKAGE_NAME}.XXXXXX")"; then
        cleanup_driver_cache_state
        return 1
    fi
    if ! curl --fail --location --proto '=https' --tlsv1.2 \
        --output "$DRIVER_TEMP" \
        "$DRIVER_URL"; then
        cleanup_driver_cache_state
        return 1
    fi
    if ! verify_driver_package "$DRIVER_TEMP"; then
        cleanup_driver_cache_state
        return 1
    fi
    if ! mv "$DRIVER_TEMP" "$DRIVER_PACKAGE"; then
        cleanup_driver_cache_state
        return 1
    fi
    DRIVER_TEMP=""
    if ! verify_driver_package "$DRIVER_PACKAGE"; then
        rm -f "$DRIVER_PACKAGE"
        cleanup_driver_cache_state
        return 1
    fi

    release_driver_lock
}

validate_no_external_symlinks() {
    local app_path="$1"
    local symlink_path=""
    local resolved_path=""
    while IFS= read -r -d '' symlink_path; do
        resolved_path="$(
            "$BUILD_VENV/bin/python" -c \
                'import os, sys; print(os.path.realpath(sys.argv[1]))' \
                "$symlink_path"
        )"
        case "$resolved_path" in
            "$app_path"/*) ;;
            *)
                echo "External bundle symlink: $symlink_path" >&2
                return 1
                ;;
        esac
    done < <(find "$app_path" -type l -print0)
}

validate_universal_macho_tree() {
    local app_path="$1"
    local macho_path=""
    local architectures=""
    while IFS= read -r -d '' macho_path; do
        if ! file -b "$macho_path" | grep -q 'Mach-O'; then
            continue
        fi
        architectures="$(lipo -archs "$macho_path")"
        if [[ " $architectures " != *" arm64 "* ]] ||
            [[ " $architectures " != *" x86_64 "* ]]; then
            echo "Non-universal Mach-O: $macho_path ($architectures)" >&2
            return 1
        fi
    done < <(find "$app_path" -type f \( -perm -111 -o -name '*.so' -o -name '*.dylib' -o -name '*.bundle' \) -print0)
}

validate_no_key_material() {
    local app_path="$1"
    if find "$app_path" -type f \( \
        -iname '*voice*key*.h' -o \
        -iname '*device*key*.h' -o \
        -iname '*provision*' -o \
        -name '*.key' \
    \) | grep -q .; then
        echo "Forbidden key or provisioning file in app bundle" >&2
        return 1
    fi
    if grep -R -I -q -x -E \
        -- '-----BEGIN [A-Z ]*PRIVATE KEY-----' \
        "$app_path"; then
        echo "Possible key material in app bundle" >&2
        return 1
    fi
    if grep -R -I -q -E \
        '(^|[^0-9A-Za-z+/])[0-9A-Fa-f]{32}([^0-9A-Za-z+/]|$)' \
        "$app_path/Contents/Resources" \
        "$app_path/Contents/MacOS" \
        "$app_path/Contents/Info.plist"; then
        echo "Possible key material in app bundle" >&2
        return 1
    fi
}

validate_info_plist_paths() {
    local app_path="$1"
    local expected_home="$APP_DIR/Contents/Frameworks/Python3.framework/Versions/$PYTHON_VERSION"
    local expected_path="$APP_DIR/Contents/Resources/app:$APP_DIR/Contents/Resources/site-packages"
    local plist="$app_path/Contents/Info.plist"
    local configured_home=""
    local configured_path=""
    configured_home="$(/usr/libexec/PlistBuddy -c 'Print :LSEnvironment:PYTHONHOME' "$plist")"
    configured_path="$(/usr/libexec/PlistBuddy -c 'Print :LSEnvironment:PYTHONPATH' "$plist")"
    [[ "$configured_home" == "$expected_home" ]]
    [[ "$configured_path" == "$expected_path" ]]
    if grep -F "$ROOT_DIR" "$plist" >/dev/null ||
        grep -F '/Library/Developer/CommandLineTools' "$plist" >/dev/null ||
        grep -F '/.venv/' "$plist" >/dev/null; then
        echo "External Python path in Info.plist" >&2
        return 1
    fi
}

smoke_test_packaged_runtime() {
    local app_path="$1"
    local executable="$app_path/Contents/MacOS/StopWatchCodexAgent"
    (
        cd "$app_path/Contents/Resources/app"
        env -i \
            HOME="$HOME" \
            PATH="/usr/bin:/bin" \
            TMPDIR="${TMPDIR:-/tmp}" \
            PYTHONHOME="$app_path/Contents/Frameworks/Python3.framework/Versions/$PYTHON_VERSION" \
            PYTHONPATH="$app_path/Contents/Resources/app:$app_path/Contents/Resources/site-packages" \
            PYTHONNOUSERSITE=1 \
            PYTHONDONTWRITEBYTECODE=1 \
            STOPWATCH_CODEX_APP=0 \
            "$executable" -B -c '
import os
import pathlib
import sys
import AppKit
import ApplicationServices
import Quartz
import Security
import bleak
import certifi
import cryptography
import PIL
from cryptography.hazmat.primitives.ciphers.aead import AESCCM
import agent
import agent_identity
import app_state
import codex_adapter
import combined_collector
import completion_classifier
import ima_adpcm
import menu_app
import protocol
import rollout_change_watcher
import runtime
import pairing_client
import secure_pairing_crypto
import secure_pairing_protocol
import secure_pairing_wire
import sitecustomize
import task_alerts
import task_detail_crypto
import trae_adapter
import trae_tasks
import voice_audio
import voice_driver
import voice_input_crypto
import voice_input_protocol
import voice_session
import voice_shortcut
import watch_credentials

app_root = pathlib.Path(os.environ["PYTHONHOME"]).resolve().parents[4]
external = []
for entry in sys.path:
    resolved = pathlib.Path(entry or os.getcwd()).resolve()
    if resolved != app_root and app_root not in resolved.parents:
        external.append(str(resolved))
if external:
    raise SystemExit("external sys.path entries")
if app_root not in pathlib.Path(certifi.where()).resolve().parents:
    raise SystemExit("external certifi bundle")
if AESCCM is None:
    raise SystemExit("AESCCM unavailable")
'
    )
}

validate_app_contents() {
    local app_path="$1"
    local resources="$app_path/Contents/Resources"
    local required_path=""
    for required_path in \
        "$app_path/Contents/MacOS/StopWatchCodexAgent" \
        "$resources/Drivers/$DRIVER_PACKAGE_NAME" \
        "$resources/native/libstopwatch_voice_audio.dylib" \
        "$resources/app/voice_driver.py" \
        "$resources/app/voice_shortcut.py" \
        "$resources/app/voice_audio.py" \
        "$resources/app/voice_input_crypto.py" \
        "$resources/app/voice_input_protocol.py" \
        "$resources/app/voice_session.py" \
        "$resources/app/typeless_transcripts.py" \
        "$resources/app/codex_task_composer.py" \
        "$resources/app/codex_app_server.py" \
        "$resources/app/task_voice_submitter.py" \
        "$resources/app/task_routes.py" \
        "$resources/app/task_detail_crypto.py" \
        "$resources/app/rollout_change_watcher.py" \
        "$resources/app/ima_adpcm.py" \
        "$resources/app/agent_identity.py" \
        "$resources/app/watch_credentials.py" \
        "$resources/app/pairing_client.py" \
        "$resources/app/secure_pairing_crypto.py" \
        "$resources/app/secure_pairing_protocol.py" \
        "$resources/app/secure_pairing_wire.py" \
        "$resources/site-packages/AppKit/__init__.py" \
        "$resources/site-packages/Quartz/__init__.py" \
        "$resources/site-packages/bleak/__init__.py" \
        "$resources/site-packages/certifi/__init__.py" \
        "$resources/site-packages/cryptography/__init__.py" \
        "$resources/site-packages/PIL/__init__.py" \
        "$app_path/Contents/Frameworks/Python3.framework/Versions/$PYTHON_VERSION/Python3"; do
        if [[ ! -f "$required_path" ]]; then
            echo "Missing packaged runtime file: $required_path" >&2
            return 1
        fi
    done
    if ! file -b "$app_path/Contents/MacOS/StopWatchCodexAgent" |
        grep -q 'Mach-O'; then
        echo "Packaged launcher is not a Mach-O executable" >&2
        return 1
    fi
    verify_driver_package "$resources/Drivers/$DRIVER_PACKAGE_NAME" || return
    if find "$app_path" -name '*.pth' -print -quit | grep -q .; then
        echo "Unexpected .pth file in app bundle" >&2
        return 1
    fi
    validate_no_external_symlinks "$app_path" || return
    validate_no_key_material "$app_path" || return
    validate_info_plist_paths "$app_path" || return
    if ! otool -D "$resources/native/libstopwatch_voice_audio.dylib" |
        grep -q '@rpath/libstopwatch_voice_audio.dylib'; then
        echo "Native audio bridge has a non-relocatable install name" >&2
        return 1
    fi
    smoke_test_packaged_runtime "$app_path" || return
}

validate_staged_app() {
    local app_path="$1"
    validate_app_contents "$app_path" || return
    validate_universal_macho_tree "$app_path" || return
    codesign --verify --deep --strict --verbose=2 "$app_path" || return
}

validate_published_app() {
    local app_path="$1"
    validate_app_contents "$app_path" || return
    validate_universal_macho_tree "$app_path" || return
    codesign --verify --deep --strict --verbose=2 "$app_path" || return
}

acquire_publish_lock() {
    mkdir -p "$(dirname "$PUBLISH_LOCK")"
    exec 8>"$PUBLISH_LOCK"
    if ! lockf -s -t 300 8; then
        exec 8>&-
        echo "Timed out waiting for app publish lock" >&2
        return 1
    fi
    PUBLISH_LOCK_OWNED=1
}

release_publish_lock() {
    if [[ "${PUBLISH_LOCK_OWNED:-0}" != "1" ]]; then
        return
    fi
    exec 8>&-
    PUBLISH_LOCK_OWNED=0
}

rollback_publish() {
    if [[ "${PUBLISH_ROLLBACK_REQUIRED:-0}" != "1" ]]; then
        return
    fi

    if [[ "$PUBLISH_HAD_PRIOR" == "1" ]]; then
        if [[ -e "$PUBLISH_BACKUP_APP" ]]; then
            rm -rf "$PUBLISH_APP_DIR"
        elif [[ -e "$PUBLISH_APP_DIR" ]]; then
            PUBLISH_ROLLBACK_REQUIRED=0
            PUBLISH_HAD_PRIOR=0
            return
        else
            echo "Prior app is missing from both publish and backup paths" >&2
            return 1
        fi
        if ! mv "$PUBLISH_BACKUP_APP" "$PUBLISH_APP_DIR"; then
            echo "Could not restore prior app from $PUBLISH_BACKUP_APP" >&2
            return 1
        fi
    else
        rm -rf "$PUBLISH_APP_DIR"
    fi
    PUBLISH_ROLLBACK_REQUIRED=0
    PUBLISH_HAD_PRIOR=0
}

publish_validated_app() {
    local staging_app="$1"
    local app_dir="$2"
    local backup_root="$3"
    local backup_app="$backup_root/StopWatch Codex Agent.app"

    STAGING_APP="$staging_app"
    validate_staged_app "$STAGING_APP" || return
    acquire_publish_lock || return
    mkdir -p "$backup_root"

    PUBLISH_APP_DIR="$app_dir"
    PUBLISH_BACKUP_APP="$backup_app"
    PUBLISH_HAD_PRIOR=0
    PUBLISH_ROLLBACK_REQUIRED=0

    if [[ -e "$app_dir" ]]; then
        PUBLISH_HAD_PRIOR=1
        PUBLISH_ROLLBACK_REQUIRED=1
        if ! mv "$app_dir" "$backup_app"; then
            PUBLISH_HAD_PRIOR=0
            PUBLISH_ROLLBACK_REQUIRED=0
            release_publish_lock
            return 1
        fi
    fi
    if ! mv "$staging_app" "$app_dir"; then
        if ! rollback_publish; then
            release_publish_lock
            return 1
        fi
        rm -rf "$backup_root"
        release_publish_lock
        return 1
    fi
    PUBLISH_ROLLBACK_REQUIRED=1

    if ! validate_published_app "$app_dir"; then
        if ! rollback_publish; then
            release_publish_lock
            return 1
        fi
        rm -rf "$backup_root"
        release_publish_lock
        return 1
    fi

    PUBLISH_ROLLBACK_REQUIRED=0
    PUBLISH_HAD_PRIOR=0
    rm -rf "$backup_root"
    release_publish_lock
}

cleanup() {
    cleanup_driver_cache_state
    if [[ "${PUBLISH_ROLLBACK_REQUIRED:-0}" == "1" ]]; then
        rollback_publish || true
    fi
    if [[ -n "${BUILD_ROOT:-}" ]]; then
        rm -rf "$BUILD_ROOT"
    fi
    if [[ -n "${BACKUP_ROOT:-}" ]] &&
        [[ "${PUBLISH_ROLLBACK_REQUIRED:-0}" != "1" ]]; then
        rm -rf "$BACKUP_ROOT"
    fi
    release_publish_lock
}

handle_signal() {
    local exit_code="$1"
    exit "$exit_code"
}

write_info_plist() {
    local plist="$1"
    cat > "$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>StopWatch Codex Agent</string>
    <key>CFBundleExecutable</key>
    <string>StopWatchCodexAgent</string>
    <key>CFBundleIdentifier</key>
    <string>$LABEL</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>StopWatch Codex Agent</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>2.0</string>
    <key>CFBundleVersion</key>
    <string>2</string>
    <key>LSUIElement</key>
    <true/>
    <key>LSEnvironment</key>
    <dict>
        <key>PYTHONHOME</key>
        <string>$APP_DIR/Contents/Frameworks/Python3.framework/Versions/$PYTHON_VERSION</string>
        <key>PYTHONPATH</key>
        <string>$APP_DIR/Contents/Resources/app:$APP_DIR/Contents/Resources/site-packages</string>
        <key>PYTHONNOUSERSITE</key>
        <string>1</string>
        <key>PYTHONDONTWRITEBYTECODE</key>
        <string>1</string>
        <key>STOPWATCH_CODEX_APP</key>
        <string>1</string>
    </dict>
    <key>NSBluetoothAlwaysUsageDescription</key>
    <string>Send local Codex status to your StopWatch over Bluetooth.</string>
</dict>
</plist>
EOF
}

build_staged_app() {
    STAGING_APP="$BUILD_ROOT/StopWatch Codex Agent.app"
    STAGING_RESOURCES="$STAGING_APP/Contents/Resources"
    STAGING_DRIVERS="$STAGING_APP/Contents/Resources/Drivers"
    STAGING_CODE="$STAGING_RESOURCES/app"
    STAGING_SITE_PACKAGES="$STAGING_RESOURCES/site-packages"
    STAGING_PYTHON_FRAMEWORK="$STAGING_APP/Contents/Frameworks/Python3.framework"
    STAGING_PYTHON_HOME="$STAGING_PYTHON_FRAMEWORK/Versions/$PYTHON_VERSION"
    STAGING_EXECUTABLE="$STAGING_APP/Contents/MacOS/StopWatchCodexAgent"

    mkdir -p \
        "$STAGING_APP/Contents/MacOS" \
        "$STAGING_DRIVERS" \
        "$STAGING_CODE" \
        "$STAGING_SITE_PACKAGES" \
        "$(dirname "$STAGING_PYTHON_FRAMEWORK")"
    mkdir -p "$STAGING_RESOURCES/native"

    cp "$DRIVER_PACKAGE" "$STAGING_DRIVERS/"
    verify_driver_package "$STAGING_DRIVERS/$DRIVER_PACKAGE_NAME"
    cp "$NATIVE_LIBRARY" "$STAGING_RESOURCES/native/libstopwatch_voice_audio.dylib"
    ditto "$PYTHON_FRAMEWORK_DIR/" "$STAGING_PYTHON_FRAMEWORK/"
    rm -rf "$STAGING_PYTHON_HOME/lib/python$PYTHON_VERSION/site-packages"
    ditto "$SITE_PACKAGES/" "$STAGING_SITE_PACKAGES/"
    cp "$PYTHON_APP_EXECUTABLE" "$STAGING_EXECUTABLE"
    install_name_tool \
        -change '@executable_path/../../../../Python3' \
        '@executable_path/../Frameworks/Python3.framework/Versions/'"$PYTHON_VERSION"'/Python3' \
        "$STAGING_EXECUTABLE"
    chmod 755 "$STAGING_EXECUTABLE"

    find "$STAGING_APP" -name '*.pth' -delete
    find "$STAGING_APP" -type d -name '__pycache__' -prune -exec rm -rf {} +
    find "$STAGING_APP" -type f \( -name '*.pyc' -o -name '*.pyo' \) -delete

    for module in \
        sitecustomize.py \
        menu_app.py \
        runtime.py \
        app_state.py \
        agent.py \
        agent_identity.py \
        codex_adapter.py \
        combined_collector.py \
        completion_classifier.py \
        codex_turn_evidence.py \
        task_alerts.py \
        trae_adapter.py \
        trae_tasks.py \
        protocol.py \
        pairing_client.py \
        secure_pairing_crypto.py \
        secure_pairing_protocol.py \
        secure_pairing_wire.py \
        voice_driver.py \
        voice_shortcut.py \
        voice_audio.py \
        voice_input_crypto.py \
        voice_input_protocol.py \
        voice_session.py \
        typeless_transcripts.py \
        codex_task_composer.py \
        codex_app_server.py \
        task_voice_submitter.py \
        task_routes.py \
        task_detail_crypto.py \
        rollout_change_watcher.py \
        watch_credentials.py \
        ima_adpcm.py; do
        cp "$ROOT_DIR/$module" "$STAGING_CODE/$module"
    done

    write_info_plist "$STAGING_APP/Contents/Info.plist"
    codesign --force --deep --sign - "$STAGING_APP"
    codesign --force --sign - --requirements \
        "$CODE_REQUIREMENT" "$STAGING_APP"
    codesign --verify --deep --strict --verbose=2 "$STAGING_APP"
}

main() {
    trap cleanup EXIT
    trap 'handle_signal 129' HUP
    trap 'handle_signal 130' INT
    trap 'handle_signal 143' TERM

    mkdir -p "$APP_PARENT" "$LOG_DIR"
    mkdir -p "$NATIVE_BUILD_DIR"
    BUILD_ROOT="$(mktemp -d "$APP_PARENT/.stopwatch-agent-build.XXXXXX")"
    BUILD_VENV="$BUILD_ROOT/build-venv"
    STAGING_APP="$BUILD_ROOT/StopWatch Codex Agent.app"
    BACKUP_ROOT=""

    ensure_driver_package

    python3 -m venv "$BUILD_VENV"
    "$BUILD_VENV/bin/python" -m pip install --upgrade pip
    "$BUILD_VENV/bin/python" -m pip install -r "$ROOT_DIR/requirements.txt"
    PILLOW_VERSION="$(
        "$BUILD_VENV/bin/python" -c \
            'import importlib.metadata; print(importlib.metadata.version("Pillow"))'
    )"
    ARCHFLAGS="-arch arm64 -arch x86_64" \
        "$BUILD_VENV/bin/python" -m pip install \
        --force-reinstall \
        --no-cache-dir \
        --no-deps \
        --no-binary=Pillow \
        "Pillow==$PILLOW_VERSION"
    CFFI_VERSION="$(
        "$BUILD_VENV/bin/python" -c \
            'import importlib.metadata; print(importlib.metadata.version("cffi"))'
    )"
    ARCHFLAGS="-arch arm64 -arch x86_64" \
        "$BUILD_VENV/bin/python" -m pip install \
        --force-reinstall \
        --no-cache-dir \
        --no-deps \
        --no-binary=cffi \
        "cffi==$CFFI_VERSION"

    SITE_PACKAGES="$(
        "$BUILD_VENV/bin/python" -c \
            'import site; print(site.getsitepackages()[0])'
    )"
    PYTHON_EXECUTABLE="$(
        "$BUILD_VENV/bin/python" -c \
            'import os, sys; print(os.path.realpath(sys.executable))'
    )"
    PYTHON_FRAMEWORK_ROOT="$(cd "$(dirname "$PYTHON_EXECUTABLE")/.." && pwd)"
    PYTHON_APP_EXECUTABLE="$PYTHON_FRAMEWORK_ROOT/Resources/Python.app/Contents/MacOS/Python"
    PYTHON_FRAMEWORK_DIR="$(cd "$PYTHON_FRAMEWORK_ROOT/../.." && pwd)"
    PYTHON_VERSION="$(
        "$BUILD_VENV/bin/python" -c \
            'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")'
    )"

    xcrun clang++ -std=c++17 -O2 -dynamiclib \
        -arch arm64 \
        -arch x86_64 \
        -install_name '@rpath/libstopwatch_voice_audio.dylib' \
        -fvisibility=hidden \
        -framework AudioToolbox \
        -framework CoreAudio \
        -framework CoreFoundation \
        "$ROOT_DIR/native/voice_audio_bridge.mm" \
        -o "$NATIVE_LIBRARY"

    build_staged_app
    BACKUP_ROOT="$(mktemp -d "$APP_PARENT/.stopwatch-agent-backup.XXXXXX")"
    publish_validated_app "$STAGING_APP" "$APP_DIR" "$BACKUP_ROOT"
    BACKUP_ROOT=""

    touch "$LOG_DIR/stopwatch-codex-agent.log"
    echo "Installed $APP_DIR"
    echo "Open StopWatch Codex Agent from your Applications folder to start it."
    echo "Log: $LOG_DIR/stopwatch-codex-agent.log"
}

if [[ "${STOPWATCH_INSTALLER_SOURCE_ONLY:-0}" != "1" ]]; then
    main "$@"
fi
