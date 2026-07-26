import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
BLACKHOLE_URL = (
    "https://existential.audio/downloads/BlackHole2ch-0.7.1.pkg"
)
BLACKHOLE_SHA256 = (
    "57b540f27a3e29c37e310e01bee0fdfab76733087e47f997ef9dccf851400dcf"
)


class InstallScriptTests(unittest.TestCase):
    def test_installer_does_not_change_agent_or_connection_state(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")

        for forbidden in (
            "launchctl",
            "Library/LaunchAgents",
            "<key>KeepAlive</key>",
            "<key>RunAtLoad</key>",
            "open -W",
            "open \"$APP_DIR\"",
            "defaults write",
            "connectionEnabled",
        ):
            self.assertNotIn(forbidden, script)

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

    def test_installer_packages_every_production_voice_module(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")

        for module in (
            "voice_driver.py",
            "voice_shortcut.py",
            "voice_audio.py",
            "voice_input_crypto.py",
            "voice_input_protocol.py",
            "voice_session.py",
            "ima_adpcm.py",
        ):
            self.assertIn(module, script)

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

    def test_installer_caches_and_packages_the_pinned_driver_without_installing_it(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")

        self.assertIn('DRIVER_CACHE="$ROOT_DIR/.driver-cache"', script)
        self.assertIn(
            'DRIVER_PACKAGE="$DRIVER_CACHE/BlackHole2ch-0.7.1.pkg"',
            script,
        )
        self.assertIn(BLACKHOLE_URL, script)
        self.assertIn(BLACKHOLE_SHA256, script)
        self.assertIn("curl --fail --location --proto '=https' --tlsv1.2", script)
        self.assertIn("Contents/Resources/Drivers", script)
        self.assertIn('trap \'rm -f "$DRIVER_TEMP"\' EXIT', script)
        self.assertIn('rm -f "$DRIVER_TEMP"', script)

        verify_before_copy = script.index('verify_driver_package "$DRIVER_PACKAGE"')
        copy_to_bundle = script.index(
            'cp "$DRIVER_PACKAGE" "$APP_RESOURCES/Drivers/"'
        )
        verify_bundle = script.index(
            'verify_driver_package "$APP_RESOURCES/Drivers/$DRIVER_PACKAGE_NAME"'
        )
        self.assertLess(verify_before_copy, copy_to_bundle)
        self.assertLess(copy_to_bundle, verify_bundle)

        for forbidden in (
            "brew",
            "/usr/sbin/installer",
            "installer -pkg",
            "osascript",
            "sudo",
        ):
            self.assertNotIn(forbidden, script)

    def test_installer_defers_key_provisioning_and_verifies_code_signature(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")

        self.assertIn("Task 10 owns external device-key provisioning", script)
        for forbidden in (
            "openssl rand",
            "/dev/urandom",
            "security add-generic-password",
            "voice_input_crypto.h",
            "xxd -i",
        ):
            self.assertNotIn(forbidden, script)
        self.assertIn(
            'codesign --verify --deep --strict --verbose=2 "$APP_DIR"',
            script,
        )


if __name__ == "__main__":
    unittest.main()
