import fcntl
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from pet_cache import PetCacheError, PetPreviewCache
from pet_package import HEIGHT, WIDTH, PetFrame, encode_package


def make_package(duration_ms=120):
    pixels = WIDTH * HEIGHT
    return encode_package(
        WIDTH,
        HEIGHT,
        (0x07E0,),
        1,
        (PetFrame(duration_ms, bytes([0]) * pixels),),
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

    def test_repeated_distinct_uploads_keep_eight_verified_packages(self):
        packages = [make_package(200 + index) for index in range(140)]
        stored = []
        rejected = []

        for index, package in enumerate(packages):
            try:
                self.cache.store(self.watch_id, package)
            except PetCacheError:
                rejected.append(package)
                continue
            stored.append(package)
            entry = self.root / self.watch_id.hex() / package.sha256.hex()
            os.utime(entry, ns=(index + 1, index + 1))

        watch_path = self.root / self.watch_id.hex()
        verified_names = {
            path.name
            for path in watch_path.iterdir()
            if len(path.name) == 64 and path.is_dir()
        }
        quarantine_names = {
            path.name
            for path in watch_path.iterdir()
            if path.name.startswith(".remove-")
        }

        self.assertEqual(len(stored), 136)
        self.assertEqual(len(rejected), 4)
        self.assertEqual(
            verified_names,
            {package.sha256.hex() for package in stored[-8:]},
        )
        self.assertEqual(len(quarantine_names), 128)
        self.assertLessEqual(
            sum(
                (
                    watch_path
                    / package.sha256.hex()
                    / "package.cxp"
                ).stat().st_size
                for package in stored[-8:]
            ),
            2 * 1024 * 1024,
        )
        self.assertIsNotNone(
            self.cache.load(self.watch_id, stored[-1].sha256)
        )
        self.assertIsNone(
            self.cache.load(self.watch_id, rejected[0].sha256)
        )

    def test_ninth_store_retires_corrupt_package_directory_from_bound(self):
        packages = [make_package(610 + index) for index in range(8)]
        for index, package in enumerate(packages):
            self.cache.store(self.watch_id, package)
            entry = self.root / self.watch_id.hex() / package.sha256.hex()
            os.utime(entry, ns=(index + 1, index + 1))

        watch_path = self.root / self.watch_id.hex()
        corrupt_path = watch_path / packages[0].sha256.hex()
        (corrupt_path / "metadata.json").write_bytes(b"corrupt")
        incoming = make_package(700)

        self.cache.store(self.watch_id, incoming)

        package_directories = [
            path
            for path in watch_path.iterdir()
            if len(path.name) == 64 and path.is_dir()
        ]
        quarantines = [
            path
            for path in watch_path.iterdir()
            if path.name.startswith(".remove-")
        ]
        self.assertEqual(len(package_directories), 8)
        self.assertFalse(corrupt_path.exists())
        self.assertIsNotNone(
            self.cache.load(self.watch_id, incoming.sha256)
        )
        self.assertEqual(len(quarantines), 1)
        self.assertEqual(
            (quarantines[0] / "package.cxp").stat().st_size,
            0,
        )
        self.assertLessEqual(
            sum(
                (path / "package.cxp").stat().st_size
                for path in package_directories
            ),
            2 * 1024 * 1024,
        )

    def test_interrupted_publication_uses_final_quarantine_slot_before_store(self):
        packages = [make_package(710 + index) for index in range(7)]
        for index, package in enumerate(packages):
            self.cache.store(self.watch_id, package)
            entry = self.root / self.watch_id.hex() / package.sha256.hex()
            os.utime(entry, ns=(index + 1, index + 1))

        watch_path = self.root / self.watch_id.hex()
        for slot in range(127):
            (watch_path / ".remove-{:02x}.tmp".format(slot)).touch()
        interrupted = make_package(800)
        interrupted_name = ".{}-0000000000000000.tmp".format(
            interrupted.sha256.hex()
        )
        interrupted_path = watch_path / interrupted_name
        interrupted_path.mkdir()
        (interrupted_path / "package.cxp").write_bytes(interrupted.encoded)
        incoming = make_package(801)

        self.cache.store(self.watch_id, incoming)

        package_directories = [
            path
            for path in watch_path.iterdir()
            if len(path.name) == 64 and path.is_dir()
        ]
        final_quarantine = watch_path / ".remove-7f.tmp"
        self.assertEqual(len(package_directories), 8)
        self.assertFalse(interrupted_path.exists())
        self.assertTrue(final_quarantine.is_dir())
        self.assertEqual(
            (final_quarantine / "package.cxp").stat().st_size,
            0,
        )
        self.assertIsNotNone(
            self.cache.load(self.watch_id, incoming.sha256)
        )

    def test_store_bounds_enumeration_of_unrelated_watch_siblings(self):
        watch_path = self.root / self.watch_id.hex()
        watch_path.mkdir()
        for index in range(1000):
            (watch_path / "unrelated-{:04d}".format(index)).touch()
        watch_inode = watch_path.stat().st_ino
        real_scandir = os.scandir
        yielded = 0

        class CountingScandir:
            def __init__(self, descriptor):
                self._entries = real_scandir(descriptor)
                self._count = (
                    isinstance(descriptor, int)
                    and os.fstat(descriptor).st_ino == watch_inode
                )

            def __enter__(self):
                return self

            def __exit__(self, *args):
                self._entries.close()

            def __iter__(self):
                return self

            def __next__(self):
                nonlocal yielded
                entry = next(self._entries)
                if self._count:
                    yielded += 1
                return entry

        with mock.patch(
            "pet_cache.os.scandir", side_effect=CountingScandir
        ):
            with self.assertRaises(PetCacheError):
                self.cache.store(self.watch_id, self.package)

        self.assertLessEqual(yielded, 137)
        self.assertFalse(
            (watch_path / self.package.sha256.hex()).exists()
        )

    def test_two_cache_instances_serialize_history_reserve_and_publish(self):
        for index in range(7):
            package = make_package(300 + index)
            self.cache.store(self.watch_id, package)
            entry = self.root / self.watch_id.hex() / package.sha256.hex()
            os.utime(entry, ns=(index + 1, index + 1))

        packages = (make_package(400), make_package(401))
        hashes = {package.sha256.hex() for package in packages}
        caches = (PetPreviewCache(self.root), PetPreviewCache(self.root))
        publication_barrier = threading.Barrier(2)
        real_rename = os.rename
        failures = []

        def synchronize_publications(source, destination, *args, **kwargs):
            if destination in hashes:
                try:
                    publication_barrier.wait(timeout=0.25)
                except threading.BrokenBarrierError:
                    pass
            return real_rename(source, destination, *args, **kwargs)

        def store(cache, package):
            try:
                cache.store(self.watch_id, package)
            except Exception as error:
                failures.append(error)

        with mock.patch(
            "pet_cache.os.rename", side_effect=synchronize_publications
        ):
            threads = [
                threading.Thread(target=store, args=pair)
                for pair in zip(caches, packages)
            ]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=2)

        self.assertTrue(all(not thread.is_alive() for thread in threads))
        self.assertEqual(failures, [])
        watch_path = self.root / self.watch_id.hex()
        verified_names = {
            path.name
            for path in watch_path.iterdir()
            if len(path.name) == 64 and path.is_dir()
        }
        self.assertEqual(len(verified_names), 8)
        self.assertTrue(hashes.issubset(verified_names))

    def test_remove_waits_for_store_transaction_before_quarantining_watch(self):
        self.cache.store(self.watch_id, self.package)
        incoming = make_package(450)
        store_cache = PetPreviewCache(self.root)
        remove_cache = PetPreviewCache(self.root)
        store_at_publish = threading.Event()
        release_store = threading.Event()
        remove_finished = threading.Event()
        failures = []
        real_rename = os.rename

        def pause_publication(source, destination, *args, **kwargs):
            if destination == incoming.sha256.hex():
                store_at_publish.set()
                if not release_store.wait(timeout=2):
                    raise RuntimeError("store release timed out")
            return real_rename(source, destination, *args, **kwargs)

        def store():
            try:
                store_cache.store(self.watch_id, incoming)
            except Exception as error:
                failures.append(error)

        def remove():
            try:
                remove_cache.remove_watch(self.watch_id)
            except Exception as error:
                failures.append(error)
            finally:
                remove_finished.set()

        with mock.patch(
            "pet_cache.os.rename", side_effect=pause_publication
        ):
            store_thread = threading.Thread(target=store)
            store_thread.start()
            self.assertTrue(store_at_publish.wait(timeout=1))
            remove_thread = threading.Thread(target=remove)
            remove_thread.start()
            removed_while_store_was_paused = remove_finished.wait(timeout=0.25)
            release_store.set()
            store_thread.join(timeout=2)
            remove_thread.join(timeout=2)

        self.assertFalse(removed_while_store_was_paused)
        self.assertFalse(store_thread.is_alive())
        self.assertFalse(remove_thread.is_alive())
        self.assertEqual(failures, [])
        self.assertFalse((self.root / self.watch_id.hex()).exists())

    def test_unlock_failure_still_closes_transaction_descriptors(self):
        root_fds = []
        watch_fds = []
        real_open_root = self.cache._open_root
        real_open_watch = self.cache._open_or_create_directory_at
        real_flock = fcntl.flock

        def record_root(*args, **kwargs):
            descriptor = real_open_root(*args, **kwargs)
            root_fds.append(descriptor)
            return descriptor

        def record_watch(*args, **kwargs):
            descriptor = real_open_watch(*args, **kwargs)
            watch_fds.append(descriptor)
            return descriptor

        def fail_unlock(descriptor, operation):
            if operation == fcntl.LOCK_UN:
                raise OSError("injected unlock failure")
            return real_flock(descriptor, operation)

        try:
            with mock.patch.object(
                self.cache, "_open_root", side_effect=record_root
            ), mock.patch.object(
                self.cache,
                "_open_or_create_directory_at",
                side_effect=record_watch,
            ), mock.patch(
                "pet_cache.fcntl.flock", side_effect=fail_unlock
            ):
                with self.assertRaises(OSError):
                    self.cache.store(self.watch_id, self.package)

            self.assertEqual(len(root_fds), 1)
            self.assertEqual(len(watch_fds), 1)
            with self.assertRaises(OSError):
                os.fstat(root_fds[0])
            with self.assertRaises(OSError):
                os.fstat(watch_fds[0])
        finally:
            for descriptor in root_fds + watch_fds:
                try:
                    os.close(descriptor)
                except OSError:
                    pass

    def test_eviction_rejects_replacement_of_verified_oldest_directory(self):
        packages = [make_package(500 + index) for index in range(8)]
        for index, package in enumerate(packages):
            self.cache.store(self.watch_id, package)
            entry = self.root / self.watch_id.hex() / package.sha256.hex()
            os.utime(entry, ns=(index + 1, index + 1))

        watch_path = self.root / self.watch_id.hex()
        oldest_name = packages[0].sha256.hex()
        saved_name = "saved-oldest"
        replacement = watch_path / "replacement"
        replacement.mkdir()
        sentinel = replacement / "sentinel"
        sentinel.write_bytes(b"replacement-sentinel")
        real_remove = self.cache._remove_bounded_at
        swapped = False

        def swap_before_removal(parent_fd, name, *args, **kwargs):
            nonlocal swapped
            if not swapped and name == oldest_name:
                os.rename(
                    name,
                    saved_name,
                    src_dir_fd=parent_fd,
                    dst_dir_fd=parent_fd,
                )
                os.rename(
                    "replacement",
                    name,
                    src_dir_fd=parent_fd,
                    dst_dir_fd=parent_fd,
                )
                swapped = True
            return real_remove(parent_fd, name, *args, **kwargs)

        incoming = make_package(600)
        with mock.patch.object(
            self.cache,
            "_remove_bounded_at",
            side_effect=swap_before_removal,
        ):
            with self.assertRaises(PetCacheError):
                self.cache.store(self.watch_id, incoming)

        self.assertTrue(swapped, "oldest-entry substitution hook did not fire")
        self.assertEqual(
            (watch_path / oldest_name / "sentinel").read_bytes(),
            b"replacement-sentinel",
        )
        self.assertTrue(
            (watch_path / saved_name / "package.cxp").stat().st_size > 0
        )
        self.assertFalse((watch_path / incoming.sha256.hex()).exists())

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

    def test_remove_streams_only_through_remaining_node_budget_plus_one(self):
        watch_path = self.root / self.watch_id.hex()
        watch_path.mkdir()
        for index in range(1000):
            (watch_path / "unrelated-{:04d}".format(index)).touch()
        watch_inode = watch_path.stat().st_ino
        real_scandir = os.scandir
        yielded = 0

        class CountingScandir:
            def __init__(self, descriptor):
                self._entries = real_scandir(descriptor)
                self._count = (
                    isinstance(descriptor, int)
                    and os.fstat(descriptor).st_ino == watch_inode
                )

            def __enter__(self):
                return self

            def __exit__(self, *args):
                self._entries.close()

            def __iter__(self):
                return self

            def __next__(self):
                nonlocal yielded
                entry = next(self._entries)
                if self._count:
                    yielded += 1
                return entry

        with mock.patch(
            "pet_cache.os.scandir", side_effect=CountingScandir
        ):
            with self.assertRaises(PetCacheError):
                self.cache.remove_watch(self.watch_id)

        self.assertLessEqual(yielded, 128)

    def test_remove_retires_remaining_budget_before_reporting_overflow(self):
        watch_path = self.root / self.watch_id.hex()
        watch_path.mkdir()
        for index in range(128):
            (watch_path / "leaf-{:03d}".format(index)).write_bytes(b"retire")

        with self.assertRaises(PetCacheError):
            self.cache.remove_watch(self.watch_id)

        quarantine = next(
            path
            for path in self.root.iterdir()
            if path.name.startswith(".remove-")
        )
        sizes = [path.stat().st_size for path in quarantine.iterdir()]
        self.assertEqual(sizes.count(0), 127)
        self.assertEqual(sizes.count(len(b"retire")), 1)

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
        real_capture = PetPreviewCache._rename_no_replace_at
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

        def capture_with_substitution(parent_fd, source, destination):
            substitute(parent_fd, source)
            return real_capture(parent_fd, source, destination)

        try:
            with mock.patch.object(
                PetPreviewCache,
                "_rename_no_replace_at",
                side_effect=capture_with_substitution,
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

    def test_remove_truncates_a_held_leaf_without_unlinking_replacement(self):
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
        real_ftruncate = os.ftruncate
        substituted = False
        unlink_calls = []

        def truncate_after_substitution(descriptor, length):
            nonlocal substituted
            if not substituted:
                captured = next(
                    path
                    for path in parent.iterdir()
                    if path.name.startswith(".remove-")
                    and path.lstat().st_ino == target_inode
                )
                real_rename(captured, parent / saved_name)
                real_rename(replacement, captured)
                substituted = True
            return real_ftruncate(descriptor, length)

        def record_unlink(name, *args, **kwargs):
            unlink_calls.append(name)
            return real_unlink(name, *args, **kwargs)

        parent_fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with mock.patch(
                "pet_cache.os.ftruncate",
                side_effect=truncate_after_substitution,
            ), mock.patch(
                "pet_cache.os.unlink", side_effect=record_unlink
            ):
                self.cache._remove_bounded_at(parent_fd, "target")
        finally:
            os.close(parent_fd)

        self.assertTrue(substituted, "post-capture substitution hook did not fire")
        self.assertEqual(unlink_calls, [])
        replacement_paths = [
            path
            for path in parent.iterdir()
            if path.lstat().st_ino == replacement_inode
        ]
        self.assertEqual(len(replacement_paths), 1)
        self.assertEqual(
            replacement_paths[0].read_bytes(), b"replacement-sentinel"
        )
        self.assertEqual((parent / saved_name).read_bytes(), b"")

    def test_removal_probes_exactly_128_fixed_quarantine_slots(self):
        parent = self.root / "slot-parent"
        parent.mkdir()
        (parent / "target").write_bytes(b"keep")
        expected_slots = [
            ".remove-{:02x}.tmp".format(slot) for slot in range(128)
        ]
        for name in expected_slots:
            (parent / name).touch()
        real_stat = os.stat
        probed = []

        def count_slot_probe(name, *args, **kwargs):
            if isinstance(name, str) and name.startswith(".remove-"):
                probed.append(name)
            return real_stat(name, *args, **kwargs)

        parent_fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with mock.patch(
                "pet_cache.os.stat", side_effect=count_slot_probe
            ):
                with self.assertRaises(PetCacheError):
                    self.cache._remove_bounded_at(parent_fd, "target")
        finally:
            os.close(parent_fd)

        self.assertEqual(probed, expected_slots)
        self.assertEqual((parent / "target").read_bytes(), b"keep")

    def test_quarantine_capture_never_clobbers_a_race_created_slot(self):
        parent = self.root / "race-slot-parent"
        parent.mkdir()
        (parent / "target").write_bytes(b"retire")
        real_exists = PetPreviewCache._entry_exists
        raced_fd = None

        def create_slot_after_missing_probe(parent_fd, name):
            nonlocal raced_fd
            exists = real_exists(parent_fd, name)
            if name == ".remove-00.tmp" and not exists and raced_fd is None:
                raced_fd = os.open(
                    name,
                    os.O_RDWR | os.O_CREAT | os.O_EXCL,
                    0o600,
                    dir_fd=parent_fd,
                )
                os.write(raced_fd, b"race-sentinel")
                os.fsync(raced_fd)
            return exists

        parent_fd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            with mock.patch.object(
                PetPreviewCache,
                "_entry_exists",
                side_effect=create_slot_after_missing_probe,
            ):
                self.cache._remove_bounded_at(parent_fd, "target")
            self.assertIsNotNone(raced_fd)
            self.assertEqual(os.fstat(raced_fd).st_nlink, 1)
            os.lseek(raced_fd, 0, os.SEEK_SET)
            self.assertEqual(os.read(raced_fd, 64), b"race-sentinel")
        finally:
            if raced_fd is not None:
                os.close(raced_fd)
            os.close(parent_fd)

        self.assertEqual(
            (parent / ".remove-00.tmp").read_bytes(), b"race-sentinel"
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
