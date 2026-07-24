import hashlib
import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from voice_driver import DriverState, detect_driver_state, verify_package


class VoiceDriverTests(unittest.TestCase):
    def setUp(self):
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.directory = pathlib.Path(self._temporary_directory.name)

    def tearDown(self):
        self._temporary_directory.cleanup()

    def test_accepts_only_the_pinned_blackhole_package(self):
        package = self.directory / "BlackHole2ch-0.7.1.pkg"
        package.write_bytes(b"known package")
        expected = hashlib.sha256(b"known package").hexdigest()
        self.assertTrue(verify_package(package, expected))
        self.assertFalse(verify_package(package, "0" * 64))

    def test_classifies_missing_receipt_and_visible_device(self):
        self.assertEqual(
            detect_driver_state(False, False), DriverState.DRIVER_REQUIRED
        )
        self.assertEqual(
            detect_driver_state(True, False), DriverState.RESTART_REQUIRED
        )
        self.assertEqual(detect_driver_state(True, True), DriverState.READY)


if __name__ == "__main__":
    unittest.main()
