import hashlib
import json
import os
import pathlib
import shlex
import subprocess
import tempfile
import textwrap
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
RELEASE_SCRIPT = ROOT / "build_release.sh"


class ReleaseBuildTests(unittest.TestCase):
    def run_release_function(self, body, *, env=None):
        command = textwrap.dedent(
            f"""
            export STOPWATCH_RELEASE_SOURCE_ONLY=1
            source {shlex.quote(str(RELEASE_SCRIPT))}
            {body}
            """
        )
        return subprocess.run(
            ["/bin/bash", "-c", command],
            capture_output=True,
            text=True,
            check=False,
            env={**os.environ, **(env or {})},
        )

    def make_release(self, directory, version="1.0.1", fingerprint="new"):
        directory.mkdir(parents=True)
        filename = f"StopWatch-Codex-Agent-{version}.dmg"
        payload = directory / filename
        payload.write_bytes(b"portable-agent")
        digest = hashlib.sha256(payload.read_bytes()).hexdigest()
        (directory / "SHA256SUMS.txt").write_text(
            f"{digest}  {filename}\n", encoding="utf-8"
        )
        (directory / "release.json").write_text(
            json.dumps(
                {
                    "version": version,
                    "build": int(version.rsplit(".", 1)[1]) + 1,
                    "source_fingerprint": fingerprint,
                    "filename": filename,
                    "sha256": digest,
                    "signing": "adhoc",
                },
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )

    def test_version_is_stable_until_release_inputs_change(self):
        cases = (
            ("1.0.0", "", "first", "1.0.0"),
            ("1.0.0", "same", "same", "1.0.0"),
            ("1.0.0", "old", "new", "1.0.1"),
            ("1.0.9", "old", "new", "1.0.10"),
        )
        for current, previous_fingerprint, fingerprint, expected in cases:
            with self.subTest(current=current, fingerprint=fingerprint):
                result = self.run_release_function(
                    "next_release_version {} {} {}".format(
                        shlex.quote(current),
                        shlex.quote(previous_fingerprint),
                        shlex.quote(fingerprint),
                    )
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)

    def test_fingerprint_changes_with_production_input_content(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            first = root / "agent.py"
            second = root / "native.mm"
            first.write_text("one\n", encoding="utf-8")
            second.write_text("two\n", encoding="utf-8")
            command = "release_input_fingerprint {} agent.py native.mm".format(
                shlex.quote(str(root))
            )

            before = self.run_release_function(command)
            first.write_text("changed\n", encoding="utf-8")
            after = self.run_release_function(command)

            self.assertEqual(before.returncode, 0, before.stderr)
            self.assertEqual(after.returncode, 0, after.stderr)
            self.assertNotEqual(before.stdout.strip(), after.stdout.strip())
            self.assertEqual(len(before.stdout.strip()), 64)

    def test_latest_publication_replaces_the_whole_previous_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            latest = root / "latest"
            staging = root / "staging"
            self.make_release(latest, version="1.0.0", fingerprint="old")
            (latest / "stale.txt").write_text("stale", encoding="utf-8")
            self.make_release(staging, version="1.0.1", fingerprint="new")

            result = self.run_release_function(
                "publish_latest_release {} {}".format(
                    shlex.quote(str(staging)), shlex.quote(str(latest))
                )
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse((latest / "stale.txt").exists())
            self.assertFalse(staging.exists())
            metadata = json.loads(
                (latest / "release.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["version"], "1.0.1")
            self.assertEqual(
                sorted(path.name for path in latest.iterdir()),
                [
                    "SHA256SUMS.txt",
                    "StopWatch-Codex-Agent-1.0.1.dmg",
                    "release.json",
                ],
            )

    def test_invalid_staging_release_preserves_current_latest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            latest = root / "latest"
            staging = root / "staging"
            self.make_release(latest, version="1.0.0", fingerprint="old")
            staging.mkdir()
            (staging / "broken.dmg").write_bytes(b"broken")

            result = self.run_release_function(
                "publish_latest_release {} {}".format(
                    shlex.quote(str(staging)), shlex.quote(str(latest))
                )
            )

            self.assertNotEqual(result.returncode, 0)
            metadata = json.loads(
                (latest / "release.json").read_text(encoding="utf-8")
            )
            self.assertEqual(metadata["version"], "1.0.0")
            self.assertTrue(staging.exists())


if __name__ == "__main__":
    unittest.main()
