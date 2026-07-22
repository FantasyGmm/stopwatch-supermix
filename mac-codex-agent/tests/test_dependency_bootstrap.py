import copy
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from dependency_bootstrap import (  # noqa: E402
    DependencyError,
    content_tree_hash,
    parse_manifest,
    write_stamp,
)


REVISION = "37db04861687858b6748f5dbd1a84439a6635c46"
PATCH_SHA256 = "5b2bd8fd54742c9f9968124d188c32dac98f8a15916af08b31f829d6b3f84778"


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


class DependencyManifestTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
