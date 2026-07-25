import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class InstallScriptTests(unittest.TestCase):
    def test_installer_removes_legacy_service_without_registering_autostart(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")

        self.assertIn('launchctl bootout "gui/$UID/$LABEL"', script)
        self.assertIn('rm -f "$PLIST"', script)
        self.assertNotIn("<key>KeepAlive</key>", script)
        self.assertNotIn("<key>RunAtLoad</key>", script)
        self.assertNotIn("launchctl bootstrap", script)
        self.assertNotIn("launchctl kickstart", script)

    def test_installer_packages_the_menu_application_modules(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")

        for module in (
            "sitecustomize.py",
            "menu_app.py",
            "runtime.py",
            "app_state.py",
            "agent.py",
            "codex_adapter.py",
            "task_alerts.py",
            "protocol.py",
        ):
            self.assertIn(module, script)
        self.assertIn("STOPWATCH_CODEX_APP", script)
        self.assertNotIn('open -W -n "$APP_DIR"', script)

    def test_installer_builds_and_packages_the_native_audio_bridge(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")

        self.assertIn('NATIVE_BUILD_DIR="$ROOT_DIR/.native"', script)
        self.assertIn(
            'NATIVE_LIBRARY="$NATIVE_BUILD_DIR/libstopwatch_voice_audio.dylib"',
            script,
        )
        self.assertIn('mkdir -p "$NATIVE_BUILD_DIR"', script)
        self.assertIn("xcrun clang++ -std=c++17 -O2 -dynamiclib", script)
        self.assertIn("-fvisibility=hidden", script)
        self.assertIn("-framework AudioToolbox", script)
        self.assertIn("-framework CoreAudio", script)
        self.assertIn("-framework CoreFoundation", script)
        self.assertIn('"$ROOT_DIR/native/voice_audio_bridge.mm"', script)
        self.assertIn('-o "$NATIVE_LIBRARY"', script)
        self.assertIn('mkdir -p "$APP_RESOURCES/native"', script)
        self.assertIn(
            'cp "$NATIVE_LIBRARY" '
            '"$APP_RESOURCES/native/libstopwatch_voice_audio.dylib"',
            script,
        )


if __name__ == "__main__":
    unittest.main()
