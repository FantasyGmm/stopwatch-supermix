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
            "completion_classifier.py",
            "task_alerts.py",
            "protocol.py",
        ):
            self.assertIn(module, script)
        self.assertIn("STOPWATCH_CODEX_APP", script)
        self.assertNotIn('open -W -n "$APP_DIR"', script)


if __name__ == "__main__":
    unittest.main()
