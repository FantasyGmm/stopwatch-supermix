import pathlib
import sys
import tempfile
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from completion import read_latest_completion_from_rollout


class CompletionDetectorTests(unittest.TestCase):
    def test_returns_latest_task_complete_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            rollout = pathlib.Path(directory) / "rollout.jsonl"
            rollout.write_text(
                '\n'.join(
                    [
                        '{"timestamp":"2026-07-11T09:00:00Z","type":"event_msg",'
                        '"payload":{"type":"task_complete"}}',
                        '{"timestamp":"2026-07-11T10:00:00Z","type":"event_msg",'
                        '"payload":{"type":"task_complete"}}',
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            result = read_latest_completion_from_rollout(rollout)

        self.assertEqual(result, "2026-07-11T10:00:00Z")


if __name__ == "__main__":
    unittest.main()
