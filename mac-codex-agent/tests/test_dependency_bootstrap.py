import copy
import dataclasses
import errno
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from dependency_bootstrap import (  # noqa: E402
    DependencyError,
    PatchSpec,
    RepoSpec,
    content_tree_hash,
    load_manifest,
    materialize_all,
    parse_manifest,
    verify_all,
    write_stamp,
)
import fetch_repos  # noqa: E402


REVISION = "37db04861687858b6748f5dbd1a84439a6635c46"
PATCH_SHA256 = "5b2bd8fd54742c9f9968124d188c32dac98f8a15916af08b31f829d6b3f84778"


class AgentPythonDependencyTests(unittest.TestCase):
    def test_requirements_pin_importable_aesccm(self):
        requirements = (ROOT / "mac-codex-agent" / "requirements.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("cryptography>=45,<47", requirements.splitlines())

        from cryptography.hazmat.primitives.ciphers.aead import AESCCM

        self.assertIsNotNone(AESCCM)


def valid_entry():
    return {
        "url": "https://github.com/m5stack/M5IOE1.git",
        "path": "components/M5IOE1",
        "revision": REVISION,
        "release": "1.0.8",
        "with_submodules": False,
        "patch": {
            "path": "patches/M5IOE1.patch",
            "sha256": PATCH_SHA256,
        },
    }


def run_git(cwd, *args, env=None):
    completed = subprocess.run(
        ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", *args],
        cwd=cwd,
        env=env,
        check=True,
        text=True,
        capture_output=True,
    )
    return completed.stdout.strip()


def create_local_upstream(root, name="upstream", content="before\n"):
    upstream = root / name
    upstream.mkdir()
    run_git(upstream, "init", "--quiet")
    run_git(upstream, "config", "user.email", "test@example.com")
    run_git(upstream, "config", "user.name", "Dependency Test")
    (upstream / "source.txt").write_text(content, encoding="utf-8")
    run_git(upstream, "add", "source.txt")
    run_git(upstream, "commit", "--quiet", "-m", "fixture")
    return upstream, run_git(upstream, "rev-parse", "HEAD")


class DependencyManifestTests(unittest.TestCase):
    def test_production_manifest_uses_verified_revisions(self):
        expected = {
            "components/mooncake": "572a7e488641f1c3f22e85d65c27f095ecf27d12",
            "components/mooncake_log": "554d55cd8c6e551b911b99cad274a46929f05bae",
            "components/smooth_ui_toolkit": "2c8b6c572192047636c78ea3ae95dc5be4955fce",
            "components/M5GFX": "53a7184601f3667b030ba141c58b87ce2acfaa2a",
            "components/ArduinoJson": "77771d3c07668e01d8f52acb03910c1110bb373f",
            "components/lvgl": "85aa60d18b3d5e5588d7b247abf90198f07c8a63",
            "components/M5IOE1": "37db04861687858b6748f5dbd1a84439a6635c46",
            "components/M5PM1": "8f1f1a60b3040088cd0ca2b9eb9d024ccc4d907f",
            "components/BMI270_BMM150_Sensor": "ae8bc480bca638d3458e65d5e9a4fff4270c1430",
        }

        actual = {
            spec.path.relative_to(ROOT).as_posix(): spec.revision
            for spec in load_manifest(ROOT)
        }

        self.assertEqual(actual, expected)

    def test_parses_exact_revision_and_patch_hash(self):
        specs = parse_manifest([valid_entry()], ROOT)

        self.assertEqual(specs[0].revision, REVISION)
        self.assertEqual(specs[0].release, "1.0.8")
        self.assertEqual(specs[0].patch.sha256, PATCH_SHA256)
        self.assertEqual(specs[0].path, ROOT / "components" / "M5IOE1")

    def test_rejects_branch_unknown_key_and_path_escape(self):
        mutations = (
            {"branch": "main"},
            {"unexpected": True},
            {"path": "../outside"},
        )
        for mutation in mutations:
            entry = valid_entry()
            entry.update(mutation)
            with self.subTest(mutation=mutation):
                with self.assertRaises(DependencyError):
                    parse_manifest([entry], ROOT)

    def test_rejects_non_https_url_and_malformed_hashes(self):
        mutations = (
            {"url": "git@github.com:m5stack/M5IOE1.git"},
            {"revision": "1.0.8"},
            {
                "patch": {
                    "path": "patches/M5IOE1.patch",
                    "sha256": "bad",
                }
            },
        )
        for mutation in mutations:
            entry = valid_entry()
            entry.update(mutation)
            with self.subTest(mutation=mutation):
                with self.assertRaises(DependencyError):
                    parse_manifest([entry], ROOT)

    def test_rejects_duplicate_component_paths(self):
        duplicate = copy.deepcopy(valid_entry())
        duplicate["url"] = "https://github.com/example/duplicate.git"

        with self.assertRaisesRegex(DependencyError, "duplicate"):
            parse_manifest([valid_entry(), duplicate], ROOT)

    def test_rejects_missing_patch_file(self):
        entry = valid_entry()
        entry["patch"]["path"] = "patches/missing.patch"

        with self.assertRaisesRegex(DependencyError, "patch"):
            parse_manifest([entry], ROOT)

    def test_content_hash_ignores_git_metadata_and_stamp(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            source = root / "source.cpp"
            source.write_text("one\n", encoding="utf-8")
            initial = content_tree_hash(root)

            (root / ".git").mkdir()
            (root / ".git" / "HEAD").write_text("ignored\n", encoding="utf-8")
            write_stamp(root, {"content_sha256": initial})

            self.assertEqual(content_tree_hash(root), initial)
            source.write_text("two\n", encoding="utf-8")
            self.assertNotEqual(content_tree_hash(root), initial)

    def test_content_hash_captures_executable_bit_and_symlink_target(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            target = root / "target.txt"
            target.write_text("content\n", encoding="utf-8")
            link = root / "link.txt"
            link.symlink_to("target.txt")

            initial = content_tree_hash(root)
            target.chmod(0o755)
            executable = content_tree_hash(root)
            link.unlink()
            link.symlink_to("other.txt")
            changed_link = content_tree_hash(root)

            self.assertNotEqual(initial, executable)
            self.assertNotEqual(executable, changed_link)


class DependencyMaterializationTests(unittest.TestCase):
    def setUp(self):
        self.root = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(self.cleanup_root)
        (self.root / "components").mkdir()
        (self.root / "patches").mkdir()
        self.upstream = None
        self.revision = None
        self.destination = self.root / "components" / "fixture"

    def cleanup_root(self):
        for attempt in range(5):
            try:
                shutil.rmtree(self.root)
                return
            except FileNotFoundError:
                return
            except OSError:
                if attempt == 4:
                    raise
                time.sleep(0.05)

    def spec(self, revision=None, patch=None, name="fixture", upstream=None):
        source = upstream
        if source is None:
            if self.upstream is None:
                self.upstream, self.revision = create_local_upstream(self.root)
            source = self.upstream
        return RepoSpec(
            url=source.as_uri(),
            path=self.root / "components" / name,
            revision=revision or self.revision,
            release="fixture",
            with_submodules=False,
            patch=patch,
        )

    def patch_spec(self):
        patch = self.root / "patches" / "fixture.patch"
        patch.write_text(
            """diff --git a/source.txt b/source.txt
--- a/source.txt
+++ b/source.txt
@@ -1 +1 @@
-before
+after
""",
            encoding="utf-8",
        )
        digest = hashlib.sha256(patch.read_bytes()).hexdigest()
        return PatchSpec(path=patch, sha256=digest)

    def test_materializes_and_checks_exact_revision(self):
        spec = self.spec()

        materialize_all([spec], self.root)

        self.assertEqual(verify_all([spec], self.root), [])
        self.assertEqual(
            run_git(self.destination, "rev-parse", "HEAD"), self.revision
        )
        stamp = json.loads(
            (self.destination / ".stopwatch-dependency.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(stamp["revision"], self.revision)

    def test_retries_transient_staging_cleanup_race(self):
        spec = self.spec()
        original_rmtree = shutil.rmtree
        cleanup_failures = 0

        def fail_first_staging_cleanup(path, *args, **kwargs):
            nonlocal cleanup_failures
            if (
                pathlib.Path(path).name.startswith(".stopwatch-dependencies-")
                and cleanup_failures == 0
            ):
                cleanup_failures += 1
                raise OSError(errno.ENOTEMPTY, "Directory not empty", str(path))
            return original_rmtree(path, *args, **kwargs)

        with mock.patch(
            "dependency_bootstrap.shutil.rmtree",
            side_effect=fail_first_staging_cleanup,
        ):
            materialize_all([spec], self.root)

        self.assertEqual(cleanup_failures, 1)
        self.assertEqual(verify_all([spec], self.root), [])
        self.assertEqual(list(self.root.glob(".stopwatch-dependencies-*")), [])

    def test_wrong_patch_hash_fails_without_replacing_destination(self):
        self.destination.mkdir()
        keep = self.destination / "keep.txt"
        keep.write_text("keep\n", encoding="utf-8")
        patch = dataclasses.replace(self.patch_spec(), sha256="0" * 64)
        unavailable = self.root / "not-cloned"

        with self.assertRaisesRegex(DependencyError, "patch hash"):
            materialize_all(
                [self.spec(patch=patch, upstream=unavailable)],
                self.root,
                force=True,
            )

        self.assertEqual(keep.read_text(encoding="utf-8"), "keep\n")

    def test_expected_patch_is_valid_but_extra_edit_is_preserved(self):
        spec = self.spec(patch=self.patch_spec())
        materialize_all([spec], self.root)
        self.assertEqual(
            (self.destination / "source.txt").read_text(encoding="utf-8"),
            "after\n",
        )
        self.assertEqual(verify_all([spec], self.root), [])

        extra = self.destination / "extra.txt"
        extra.write_text("user change\n", encoding="utf-8")
        with self.assertRaisesRegex(DependencyError, "local changes"):
            materialize_all([spec], self.root)
        self.assertTrue(extra.exists())

    def test_verification_reports_missing_stamp_wrong_head_and_changed_content(self):
        spec = self.spec()
        self.destination.mkdir()
        issues = verify_all([spec], self.root)
        self.assertEqual(issues[0].category, "missing_stamp")

        materialize_all([spec], self.root, force=True)
        (self.destination / "source.txt").write_text("changed\n", encoding="utf-8")
        issues = verify_all([spec], self.root)
        self.assertEqual(issues[0].category, "content_changed")

        run_git(self.destination, "checkout", "--quiet", "--", "source.txt")
        run_git(self.destination, "checkout", "--quiet", "--detach", "HEAD~0")
        wrong_spec = dataclasses.replace(spec, revision="0" * 40)
        issues = verify_all([wrong_spec], self.root)
        self.assertEqual(issues[0].category, "wrong_revision")

    def test_one_failed_stage_leaves_all_existing_destinations_untouched(self):
        first = self.root / "components" / "first"
        second = self.root / "components" / "second"
        first.mkdir()
        second.mkdir()
        (first / "keep.txt").write_text("first\n", encoding="utf-8")
        (second / "keep.txt").write_text("second\n", encoding="utf-8")
        good = self.spec(name="first")
        bad = self.spec(revision="0" * 40, name="second")

        with self.assertRaises(DependencyError):
            materialize_all([good, bad], self.root, force=True)

        self.assertEqual((first / "keep.txt").read_text(), "first\n")
        self.assertEqual((second / "keep.txt").read_text(), "second\n")

    def test_records_initialized_submodule_revision(self):
        submodule, submodule_revision = create_local_upstream(
            self.root, "submodule", "nested\n"
        )
        parent = self.root / "parent"
        parent.mkdir()
        run_git(parent, "init", "--quiet")
        run_git(parent, "config", "user.email", "test@example.com")
        run_git(parent, "config", "user.name", "Dependency Test")
        env = os.environ.copy()
        env["GIT_ALLOW_PROTOCOL"] = "file"
        run_git(
            parent,
            "submodule",
            "add",
            "--quiet",
            submodule.as_uri(),
            "nested",
            env=env,
        )
        run_git(parent, "commit", "--quiet", "-am", "add submodule")
        revision = run_git(parent, "rev-parse", "HEAD")
        spec = dataclasses.replace(
            self.spec(revision=revision, upstream=parent), with_submodules=True
        )

        with mock.patch.dict(os.environ, {"GIT_ALLOW_PROTOCOL": "file"}):
            materialize_all([spec], self.root)

        stamp = json.loads(
            (self.destination / ".stopwatch-dependency.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(stamp["submodules"], {"nested": submodule_revision})
        self.assertEqual(verify_all([spec], self.root), [])


class FetchReposCliTests(unittest.TestCase):
    def test_check_returns_nonzero_and_prints_issues(self):
        issue = mock.Mock(
            category="missing", path=pathlib.Path("components/test"), message="absent"
        )
        with mock.patch.object(fetch_repos, "load_manifest", return_value=[]):
            with mock.patch.object(fetch_repos, "verify_all", return_value=[issue]):
                self.assertEqual(fetch_repos.main(["--check"]), 1)

    def test_check_and_force_are_mutually_exclusive(self):
        with self.assertRaises(SystemExit):
            fetch_repos.parse_args(["--check", "--force"])


if __name__ == "__main__":
    unittest.main()
