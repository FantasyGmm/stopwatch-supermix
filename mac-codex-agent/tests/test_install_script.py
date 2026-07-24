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
            "trae_tasks.py",
            "trae_adapter.py",
            "combined_collector.py",
            "completion_classifier.py",
            "task_alerts.py",
            "protocol.py",
            "trae_tasks.py",
            "trae_adapter.py",
            "combined_collector.py",
        ):
            self.assertIn(module, script)
        self.assertIn("STOPWATCH_CODEX_APP", script)
        self.assertNotIn('open -W -n "$APP_DIR"', script)

    def test_packaging_preserves_existing_product_identity(self):
        script = (ROOT / "install_launch_agent.sh").read_text(encoding="utf-8")
        agent = (ROOT / "agent.py").read_text(encoding="utf-8")

        self.assertIn('LABEL="com.stopwatch.codex-agent"', script)
        self.assertIn("StopWatch Codex Agent.app", script)
        self.assertIn("<string>StopWatch Codex Agent</string>", script)
        self.assertIn('DEVICE_NAME = "StopWatch-Codex"', agent)
        self.assertIn(
            'SERVICE_UUID = "7a230001-5474-4f50-5741-544348434f44"',
            agent,
        )


if __name__ == "__main__":
    unittest.main()
