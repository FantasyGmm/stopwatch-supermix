import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from pet_cache import PetCacheError, PetPreviewCache
from pet_package import HEIGHT, WIDTH, PetFrame, encode_package


def make_package():
    pixels = WIDTH * HEIGHT
    return encode_package(
        WIDTH,
        HEIGHT,
        (0x07E0,),
        1,
        (PetFrame(120, bytes([0]) * pixels),),
    )


class PetPreviewCacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name).resolve()
        self.cache = PetPreviewCache(self.root)
        self.watch_id = bytes(range(16))
        self.package = make_package()

    def tearDown(self):
        self.temporary.cleanup()

    def test_store_publishes_only_canonical_package_and_verified_preview(self):
        self.cache.store(self.watch_id, self.package)

        preview = self.cache.load(self.watch_id, self.package.sha256)
        package_path = (
            self.root
            / self.watch_id.hex()
            / self.package.sha256.hex()
            / "package.cxp"
        )

        self.assertIsNotNone(preview)
        self.assertEqual(preview.package.encoded, self.package.encoded)
        self.assertEqual(preview.rgba_frames[0][:4], bytes((0, 255, 0, 255)))
        self.assertEqual(package_path.read_bytes(), self.package.encoded)
        self.assertEqual(
            sorted(path.name for path in package_path.parent.iterdir()),
            ["metadata.json", "package.cxp"],
        )

    def test_authoritative_watch_identity_and_hash_gate_reads(self):
        self.cache.store(self.watch_id, self.package)

        self.assertIsNone(self.cache.load(self.watch_id, bytes(32)))
        self.assertIsNone(self.cache.load(b"X" * 16, self.package.sha256))
        self.assertIsNone(self.cache.load(self.watch_id, b"Y" * 32))

        package_path = (
            self.root
            / self.watch_id.hex()
            / self.package.sha256.hex()
            / "package.cxp"
        )
        corrupted = bytearray(self.package.encoded)
        corrupted[-1] ^= 0x01
        package_path.write_bytes(corrupted)
        self.assertIsNone(self.cache.load(self.watch_id, self.package.sha256))

        metadata_path = package_path.parent / "metadata.json"
        package_path.write_bytes(self.package.encoded)
        metadata_path.write_bytes(b"x" * 4097)
        self.assertIsNone(self.cache.load(self.watch_id, self.package.sha256))

        self.cache.store(self.watch_id, self.package)
        self.assertEqual(
            self.cache.load(self.watch_id, self.package.sha256).package,
            self.package,
        )

    def test_remove_watch_is_scoped_and_bounded(self):
        self.cache.store(self.watch_id, self.package)
        sibling = self.root / "keep"
        sibling.mkdir()
        (sibling / "sentinel").write_text("keep", encoding="utf-8")

        self.cache.remove_watch(self.watch_id)

        self.assertFalse((self.root / self.watch_id.hex()).exists())
        self.assertEqual((sibling / "sentinel").read_text(encoding="utf-8"), "keep")

    def test_remove_watch_unlinks_symlink_without_traversing_it(self):
        outside = self.root / "outside"
        outside.mkdir()
        sentinel = outside / "sentinel"
        sentinel.write_text("keep", encoding="utf-8")
        watch_path = self.root / self.watch_id.hex()
        watch_path.mkdir()
        os.symlink(outside, watch_path / "link")

        self.cache.remove_watch(self.watch_id)

        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_symlinked_cache_root_is_never_read_written_or_traversed(self):
        target = self.root / "target"
        target.mkdir()
        target_cache = PetPreviewCache(target)
        target_cache.store(self.watch_id, self.package)
        sentinel = target / self.watch_id.hex() / "sentinel"
        sentinel.write_text("keep", encoding="utf-8")
        linked_root = self.root / "Pets"
        os.symlink(target, linked_root)
        linked_cache = PetPreviewCache(linked_root)

        self.assertIsNone(
            linked_cache.load(self.watch_id, self.package.sha256)
        )
        with self.assertRaises(PetCacheError):
            linked_cache.store(self.watch_id, self.package)
        linked_cache.remove_watch(self.watch_id)

        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_symlinked_cache_ancestor_is_rejected_without_traversal(self):
        target_parent = self.root / "target-parent"
        target_root = target_parent / "Pets"
        target_root.mkdir(parents=True)
        target_cache = PetPreviewCache(target_root)
        target_cache.store(self.watch_id, self.package)
        sentinel = target_root / self.watch_id.hex() / "sentinel"
        sentinel.write_text("keep", encoding="utf-8")
        linked_parent = self.root / "linked-parent"
        os.symlink(target_parent, linked_parent)
        linked_cache = PetPreviewCache(linked_parent / "Pets")

        self.assertIsNone(
            linked_cache.load(self.watch_id, self.package.sha256)
        )
        with self.assertRaises(PetCacheError):
            linked_cache.store(self.watch_id, self.package)
        linked_cache.remove_watch(self.watch_id)

        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_fifo_cache_leaf_is_rejected_without_blocking(self):
        package_path = (
            self.root
            / self.watch_id.hex()
            / self.package.sha256.hex()
            / "package.cxp"
        )
        package_path.parent.mkdir(parents=True)
        os.mkfifo(package_path)
        code = """
import pathlib
import sys
from pet_cache import PetPreviewCache

root = pathlib.Path(sys.argv[1])
watch_id = bytes.fromhex(sys.argv[2])
package_hash = bytes.fromhex(sys.argv[3])
if PetPreviewCache(root).load(watch_id, package_hash) is not None:
    raise SystemExit(1)
"""
        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(pathlib.Path(__file__).parents[1])
        try:
            completed = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    code,
                    str(self.root),
                    self.watch_id.hex(),
                    self.package.sha256.hex(),
                ],
                env=environment,
                timeout=0.5,
                check=False,
            )
        except subprocess.TimeoutExpired:
            self.fail("cache read blocked while opening FIFO")
        self.assertEqual(completed.returncode, 0)

    def test_store_never_re_resolves_swapped_watch_directory(self):
        with tempfile.TemporaryDirectory() as outside_name:
            outside = pathlib.Path(outside_name)
            sentinel = outside / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")
            watch_path = self.root / self.watch_id.hex()
            saved = self.root / "saved-watch"
            real_rename = os.rename
            swapped = False

            def swap_before_publish(source, destination, *args, **kwargs):
                nonlocal swapped
                if (
                    not swapped
                    and kwargs.get("src_dir_fd") is not None
                    and kwargs.get("dst_dir_fd") is not None
                    and destination == self.package.sha256.hex()
                ):
                    real_rename(watch_path, saved)
                    os.symlink(outside, watch_path)
                    swapped = True
                return real_rename(source, destination, *args, **kwargs)

            try:
                with mock.patch(
                    "pet_cache.os.rename", side_effect=swap_before_publish
                ):
                    self.cache.store(self.watch_id, self.package)
            finally:
                if swapped:
                    watch_path.unlink(missing_ok=True)
                    saved.rename(watch_path)

            self.assertTrue(swapped, "descriptor-relative rename hook did not fire")
            self.assertEqual(
                sorted(path.name for path in outside.iterdir()), ["sentinel"]
            )

    def test_load_never_re_resolves_swapped_hash_directory(self):
        self.cache.store(self.watch_id, self.package)
        entry = (
            self.root / self.watch_id.hex() / self.package.sha256.hex()
        )
        saved = entry.parent / "saved-entry"
        with tempfile.TemporaryDirectory() as outside_name:
            outside = pathlib.Path(outside_name)
            sentinel = outside / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")
            real_open = os.open
            real_rename = os.rename
            swapped = False

            def swap_before_leaf_open(path, flags, *args, **kwargs):
                nonlocal swapped
                if (
                    not swapped
                    and path == "package.cxp"
                    and kwargs.get("dir_fd") is not None
                ):
                    real_rename(entry, saved)
                    os.symlink(outside, entry)
                    swapped = True
                return real_open(path, flags, *args, **kwargs)

            with mock.patch(
                "pet_cache.os.open", side_effect=swap_before_leaf_open
            ):
                preview = self.cache.load(
                    self.watch_id, self.package.sha256
                )
            if entry.is_symlink():
                entry.unlink()
                saved.rename(entry)

            self.assertTrue(swapped, "descriptor-relative open hook did not fire")
            self.assertIsNotNone(preview)
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_remove_never_re_resolves_swapped_watch_directory(self):
        self.cache.store(self.watch_id, self.package)
        watch_path = self.root / self.watch_id.hex()
        saved = self.root / "saved-watch"
        with tempfile.TemporaryDirectory() as outside_name:
            outside = pathlib.Path(outside_name)
            sentinel = outside / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")
            real_scandir = os.scandir
            real_rename = os.rename
            watch_inode = watch_path.stat().st_ino
            swapped = False
            swapped_path = None

            def swap_before_scan(path):
                nonlocal swapped, swapped_path
                if (
                    not swapped
                    and isinstance(path, int)
                    and os.fstat(path).st_ino == watch_inode
                ):
                    swapped_path = next(
                        candidate
                        for candidate in self.root.iterdir()
                        if candidate.lstat().st_ino == watch_inode
                    )
                    real_rename(swapped_path, saved)
                    os.symlink(outside, swapped_path)
                    swapped = True
                return real_scandir(path)

            try:
                with mock.patch(
                    "pet_cache.os.scandir", side_effect=swap_before_scan
                ):
                    try:
                        self.cache.remove_watch(self.watch_id)
                    except (OSError, PetCacheError):
                        pass
            finally:
                if swapped:
                    swapped_path.unlink(missing_ok=True)
                    saved.rename(watch_path)

            self.assertTrue(swapped, "descriptor-relative scandir hook did not fire")
            self.assertTrue(sentinel.exists(), "removal traversed outside cache")
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_remove_never_deletes_a_pre_capture_replacement(self):
        self.cache.store(self.watch_id, self.package)
        watch_path = self.root / self.watch_id.hex()
        saved = self.root / "saved-watch"
        replacement = self.root / "replacement"
        replacement.mkdir()
        replacement_inode = replacement.stat().st_ino
        watch_inode = watch_path.stat().st_ino
        root_inode = self.root.stat().st_ino
        real_rename = os.rename
        real_rmdir = os.rmdir
        sentinel = self.root / "sentinel"
        sentinel.write_text("keep", encoding="utf-8")
        substituted = False

        def substitute(parent_fd, name):
            nonlocal substituted
            if substituted or parent_fd is None:
                return
            try:
                parent_is_root = os.fstat(parent_fd).st_ino == root_inode
                target_is_watch = (
                    os.stat(
                        name, dir_fd=parent_fd, follow_symlinks=False
                    ).st_ino
                    == watch_inode
                )
            except OSError:
                return
            if parent_is_root and target_is_watch:
                real_rename(watch_path, saved)
                real_rename(replacement, watch_path)
                substituted = True

        def rename_with_substitution(source, destination, *args, **kwargs):
            substitute(kwargs.get("src_dir_fd"), source)
            return real_rename(source, destination, *args, **kwargs)

        def rmdir_with_substitution(name, *args, **kwargs):
            substitute(kwargs.get("dir_fd"), name)
            return real_rmdir(name, *args, **kwargs)

        try:
            with mock.patch(
                "pet_cache.os.rename", side_effect=rename_with_substitution
            ), mock.patch(
                "pet_cache.os.rmdir", side_effect=rmdir_with_substitution
            ):
                try:
                    self.cache.remove_watch(self.watch_id)
                except PetCacheError:
                    pass
            replacement_survived = any(
                path.lstat().st_ino == replacement_inode
                for path in self.root.iterdir()
            )
        finally:
            if substituted:
                for path in self.root.iterdir():
                    if (
                        path.is_dir()
                        and path.stat().st_ino == replacement_inode
                    ):
                        path.rmdir()
                saved.rename(watch_path)

        self.assertTrue(substituted, "pre-removal substitution hook did not fire")
        self.assertTrue(
            replacement_survived, "removal deleted a substituted directory"
        )
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep")

    def test_remove_never_unlinks_a_post_capture_replacement_leaf(self):
        parent = self.root / "leaf-parent"
        parent.mkdir()
        target = parent / "target"
        target.write_bytes(b"original")
        target_inode = target.stat().st_ino
        replacement = parent / "replacement"
        replacement.write_bytes(b"replacement-sentinel")
        replacement_inode = replacement.stat().st_ino
        saved_name = "saved-original"
        real_rename = os.rename
        real_unlink = os.unlink
        substituted = False

        def substitute(parent_fd, name):
            nonlocal substituted
            if substituted or parent_fd is None or not name.startswith(".remove-"):
                return
            try:
                is_target = (
                    os.stat(
                        name, dir_fd=parent_fd, follow_symlinks=False
                    ).st_ino
                    == target_inode
                )
            except OSError:
                return
            if is_target:
                real_rename(
                    name,
                    saved_name,
                    src_dir_fd=parent_fd,
                    dst_dir_fd=parent_fd,
                )
                real_rename(
                    "replacement",
                    name,
                    src_dir_fd=parent_fd,
                    dst_dir_fd=parent_fd,
                )
                substituted = True

        def rename_with_substitution(source, destination, *args, **kwargs):
            substitute(kwargs.get("src_dir_fd"), source)
            return real_rename(source, destination, *args, **kwargs)

        def unlink_with_substitution(name, *args, **kwargs):
            substitute(kwargs.get("dir_fd"), name)
            return real_unlink(name, *args, **kwargs)

        parent_fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with mock.patch(
                "pet_cache.os.rename", side_effect=rename_with_substitution
            ), mock.patch(
                "pet_cache.os.unlink", side_effect=unlink_with_substitution
            ):
                try:
                    self.cache._remove_bounded_at(parent_fd, "target")
                except PetCacheError:
                    pass
        finally:
            os.close(parent_fd)

        replacement_paths = [
            path
            for path in parent.iterdir()
            if path.lstat().st_ino == replacement_inode
        ]
        self.assertTrue(substituted, "post-capture substitution hook did not fire")
        self.assertEqual(len(replacement_paths), 1)
        self.assertEqual(
            replacement_paths[0].read_bytes(), b"replacement-sentinel"
        )

    def test_repeated_removal_leaves_a_strictly_bounded_quarantine_pool(self):
        failures = 0
        for _ in range(256):
            self.cache.store(self.watch_id, self.package)
            try:
                self.cache.remove_watch(self.watch_id)
            except PetCacheError:
                failures += 1

        quarantines = [
            path
            for path in self.root.iterdir()
            if path.name.startswith(".remove-")
        ]
        self.assertGreater(failures, 0)
        self.assertLessEqual(len(quarantines), 128)


if __name__ == "__main__":
    unittest.main()
