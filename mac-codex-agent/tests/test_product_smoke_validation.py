import importlib.util
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = ROOT / "scripts" / "validate_product_smoke.py"


def load_module():
    spec = importlib.util.spec_from_file_location("validate_product_smoke", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProductSmokeValidationTests(unittest.TestCase):
    def test_tracks_catalog_and_complete_app_round_trip(self):
        module = load_module()
        validator = module.ProductSmokeValidator(["Codex", "Clock"])
        lines = (
            "I DebugCommand: DEBUG APP LIST codex,assistant,clock,badge,imu,audio-fft,settings",
            "[123] [info] [Codex] on open",
            "[124] [info] [Codex] on close",
            "[125] [info] [Launcher] on open",
            "[126] [info] [Clock] on open",
            "[127] [info] [Clock] on close",
            "[128] [info] [Launcher] on open",
        )
        for line in lines:
            validator.feed(line)

        validator.require_catalog()
        validator.require_round_trip("Codex", 0, 0, 0)
        validator.require_round_trip("Clock", 0, 0, 1)

    def test_rejects_missing_app_and_reset_markers(self):
        module = load_module()
        validator = module.ProductSmokeValidator(["Codex", "Clock"])
        validator.feed("DEBUG APP LIST codex")
        with self.assertRaisesRegex(module.ProductSmokeError, "missing applications"):
            validator.require_catalog()

        with self.assertRaisesRegex(module.ProductSmokeError, "reset marker"):
            validator.feed("Guru Meditation Error: Core 0 panic'ed")


if __name__ == "__main__":
    unittest.main()
