import pathlib
import sys
import tempfile
import threading
import time
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from rollout_change_watcher import capture_signatures, wait_for_path_change


class RolloutChangeWatcherTests(unittest.TestCase):
    def test_append_wakes_before_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "rollout.jsonl"
            path.touch()
            baseline = capture_signatures((path,))
            timer = threading.Timer(0.05, path.write_text, args=("event\n",))
            timer.start()
            started = time.monotonic()
            try:
                changed = wait_for_path_change((path,), baseline, 1.0)
            finally:
                timer.join()

        self.assertTrue(changed)
        self.assertLess(time.monotonic() - started, 0.8)

    def test_no_change_waits_for_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "rollout.jsonl"
            path.touch()
            baseline = capture_signatures((path,))
            started = time.monotonic()

            changed = wait_for_path_change((path,), baseline, 0.05)

        self.assertFalse(changed)
        self.assertGreaterEqual(time.monotonic() - started, 0.04)

    def test_change_before_registration_returns_immediately(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "rollout.jsonl"
            path.touch()
            baseline = capture_signatures((path,))
            path.write_text("event\n", encoding="utf-8")
            started = time.monotonic()

            changed = wait_for_path_change((path,), baseline, 1.0)

        self.assertTrue(changed)
        self.assertLess(time.monotonic() - started, 0.1)


if __name__ == "__main__":
    unittest.main()
