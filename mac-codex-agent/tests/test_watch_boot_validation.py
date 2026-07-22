import importlib.util
import os
import pathlib
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "scripts" / "validate_watch_boot.py"


def load_module():
    spec = importlib.util.spec_from_file_location("validate_watch_boot", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class WatchBootValidationTests(unittest.TestCase):
    def test_accepts_reset_reason_followed_by_launcher(self):
        module = load_module()
        validator = module.WatchBootValidator()
        validator.feed("rst:0x1 (POWERON_RESET),boot:0x8")
        validator.feed("I Launcher: on open")
        self.assertTrue(validator.ready)
        self.assertEqual(validator.reset_line, "rst:0x1 (POWERON_RESET),boot:0x8")

    def test_accepts_launcher_debug_catalog_when_open_log_was_missed(self):
        module = load_module()
        validator = module.WatchBootValidator()
        validator.feed(
            "I DebugCommand: DEBUG APP LIST codex,assistant,clock,badge,imu,audio-fft,settings"
        )
        self.assertTrue(validator.ready)

    def test_rejects_fatal_boot_output(self):
        module = load_module()
        validator = module.WatchBootValidator()
        with self.assertRaisesRegex(module.WatchBootError, "fatal boot marker"):
            validator.feed("Guru Meditation Error: Core 0 panic'ed")

    def test_reset_uses_explicit_idf_python_from_hardware_campaign(self):
        module = load_module()
        resolver = getattr(module, "esptool_python", lambda: None)
        with mock.patch.dict(os.environ, {"PYTHON": "/idf/python"}):
            self.assertEqual(resolver(), "/idf/python")


if __name__ == "__main__":
    unittest.main()
