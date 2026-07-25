#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV_DIR="$ROOT_DIR/.venv"
LABEL="com.stopwatch.codex-agent"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs"
APP_DIR="$HOME/Applications/StopWatch Codex Agent.app"
APP_EXECUTABLE="$APP_DIR/Contents/MacOS/StopWatchCodexAgent"
APP_RESOURCES="$APP_DIR/Contents/Resources"
APP_CODE="$APP_RESOURCES/app"
APP_PYTHON_RUNTIME="$APP_DIR/Contents/Python3"
NATIVE_BUILD_DIR="$ROOT_DIR/.native"
NATIVE_LIBRARY="$NATIVE_BUILD_DIR/libstopwatch_voice_audio.dylib"

# Stop and remove the legacy auto-start service before replacing its executable.
launchctl bootout "gui/$UID/$LABEL" >/dev/null 2>&1 || true
rm -f "$PLIST"

python3 -m venv "$VENV_DIR"
"$VENV_DIR/bin/python" -m pip install --upgrade pip
"$VENV_DIR/bin/python" -m pip install -r "$ROOT_DIR/requirements.txt"

mkdir -p "$NATIVE_BUILD_DIR"
xcrun clang++ -std=c++17 -O2 -dynamiclib \
    -fvisibility=hidden \
    -framework AudioToolbox \
    -framework CoreAudio \
    -framework CoreFoundation \
    "$ROOT_DIR/native/voice_audio_bridge.mm" \
    -o "$NATIVE_LIBRARY"

SITE_PACKAGES="$($VENV_DIR/bin/python -c 'import site; print(site.getsitepackages()[0])')"
PYTHON_EXECUTABLE="$($VENV_DIR/bin/python -c 'import os, sys; print(os.path.realpath(sys.executable))')"
PYTHON_FRAMEWORK_ROOT="$(cd "$(dirname "$PYTHON_EXECUTABLE")/.." && pwd)"
PYTHON_APP_EXECUTABLE="$PYTHON_FRAMEWORK_ROOT/Resources/Python.app/Contents/MacOS/Python"
PYTHON_RUNTIME="$PYTHON_FRAMEWORK_ROOT/Python3"

mkdir -p "$LOG_DIR" "$APP_DIR/Contents/MacOS" "$APP_RESOURCES" "$APP_CODE"
mkdir -p "$APP_RESOURCES/native"
cp "$NATIVE_LIBRARY" "$APP_RESOURCES/native/libstopwatch_voice_audio.dylib"
cp "$PYTHON_APP_EXECUTABLE" "$APP_EXECUTABLE"
cp "$PYTHON_RUNTIME" "$APP_PYTHON_RUNTIME"
install_name_tool \
    -change '@executable_path/../../../../Python3' \
    '@executable_path/../Python3' \
    "$APP_EXECUTABLE"
chmod 755 "$APP_EXECUTABLE"

rm -rf "$APP_CODE"
mkdir -p "$APP_CODE"
for module in \
    sitecustomize.py \
    menu_app.py \
    runtime.py \
    app_state.py \
    agent.py \
    codex_adapter.py \
    task_alerts.py \
    protocol.py; do
    cp "$ROOT_DIR/$module" "$APP_CODE/$module"
done
rm -f "$APP_RESOURCES/request_bluetooth.py"

cat > "$APP_DIR/Contents/Info.plist" <<EOF
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
        <string>$PYTHON_FRAMEWORK_ROOT</string>
        <key>PYTHONPATH</key>
        <string>$APP_CODE:$SITE_PACKAGES</string>
        <key>STOPWATCH_CODEX_APP</key>
        <string>1</string>
    </dict>
    <key>NSBluetoothAlwaysUsageDescription</key>
    <string>Send local Codex status to your StopWatch over Bluetooth.</string>
</dict>
</plist>
EOF

codesign --force --deep --sign - "$APP_DIR"

touch "$LOG_DIR/stopwatch-codex-agent.log"

echo "Installed $APP_DIR"
echo "Open StopWatch Codex Agent from your Applications folder to start it."
echo "Log: $LOG_DIR/stopwatch-codex-agent.log"
