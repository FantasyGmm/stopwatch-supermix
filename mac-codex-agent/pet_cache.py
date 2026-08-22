"""Authoritative watch-identity/hash keyed cache for canonical pet previews."""

from __future__ import annotations

import dataclasses
import json
import os
import pathlib
import secrets
import stat
from typing import Optional

from pet_package import (
    MAX_PACKAGE_BYTES,
    SCHEMA_VERSION,
    PetPackage,
    parse_package,
    render_rgba_frames,
)


PACKAGE_NAME = "package.cxp"
METADATA_NAME = "metadata.json"
MAX_METADATA_BYTES = 4096
MAX_REMOVAL_NODES = 128
MAX_REMOVAL_BYTES = 64 * 1024 * 1024
MAX_REMOVAL_DEPTH = 4
MAX_REMOVAL_QUARANTINES = 128


class PetCacheError(RuntimeError):
    """A local preview cache entry failed bounded validation."""


@dataclasses.dataclass(frozen=True)
class CachedPreview:
    package: PetPackage
    rgba_frames: tuple[bytes, ...]
    durations_ms: tuple[int, ...]


class PetPreviewCache:
    def __init__(self, root: Optional[pathlib.Path] = None) -> None:
        self._root = pathlib.Path(root) if root is not None else (
            pathlib.Path.home()
            / "Library"
            / "Application Support"
            / "StopWatch Codex Agent"
            / "Pets"
        )

    def store(self, watch_id: bytes, package: PetPackage) -> None:
        watch_id = self._watch_id(watch_id)
        canonical = self._canonical(package)
        self._publish_atomic(watch_id, canonical)

    def load(
        self, watch_id: bytes, active_hash: bytes
    ) -> Optional[CachedPreview]:
        watch_id = self._watch_id(watch_id)
        if (
            isinstance(active_hash, bytes)
            and len(active_hash) == 32
            and not any(active_hash)
        ):
            return None
        active_hash = self._hash(active_hash)
        root_fd = self._open_root(create=False)
        if root_fd is None:
            return None
        watch_fd = None
        entry_fd = None
        try:
            watch_fd = self._open_directory_at(root_fd, watch_id.hex())
            entry_fd = self._open_directory_at(watch_fd, active_hash.hex())
            return self._load_verified_fd(entry_fd, active_hash)
        except (OSError, ValueError, TypeError, PetCacheError):
            return None
        finally:
            self._close_fds(entry_fd, watch_fd, root_fd)

    def remove_watch(self, watch_id: bytes) -> None:
        watch_name = self._watch_id(watch_id).hex()
        root_fd = self._open_root(create=False)
        if root_fd is None:
            return
        try:
            if self._entry_exists(root_fd, watch_name):
                self._remove_bounded_at(root_fd, watch_name)
                os.fsync(root_fd)
        finally:
            os.close(root_fd)

    def _publish_atomic(
        self, watch_id: bytes, package: PetPackage
    ) -> None:
        root_fd = None
        watch_fd = None
        entry_fd = None
        temporary_fd = None
        temporary_name = None
        try:
            root_fd = self._open_root(create=True)
            watch_fd = self._open_or_create_directory_at(
                root_fd, watch_id.hex()
            )
            destination_name = package.sha256.hex()
            try:
                entry_fd = self._open_directory_at(
                    watch_fd, destination_name
                )
            except OSError:
                entry_fd = None
            if entry_fd is not None:
                existing = self._load_verified_fd(entry_fd, package.sha256)
                os.close(entry_fd)
                entry_fd = None
                if existing is not None:
                    return
            if self._entry_exists(watch_fd, destination_name):
                self._remove_bounded_at(watch_fd, destination_name)

            temporary_name, temporary_fd = self._create_temporary_directory(
                watch_fd, package.sha256
            )
            self._write_fsynced_at(
                temporary_fd, PACKAGE_NAME, package.encoded
            )
            self._write_fsynced_at(
                temporary_fd,
                METADATA_NAME,
                json.dumps(
                    self._metadata(package),
                    sort_keys=True,
                    separators=(",", ":"),
                ).encode("utf-8"),
            )
            os.fsync(temporary_fd)
            os.close(temporary_fd)
            temporary_fd = None
            os.rename(
                temporary_name,
                destination_name,
                src_dir_fd=watch_fd,
                dst_dir_fd=watch_fd,
            )
            temporary_name = None
            os.fsync(watch_fd)
        except PetCacheError:
            raise
        except Exception as error:
            raise PetCacheError("cache publication failed") from error
        finally:
            if temporary_fd is not None:
                os.close(temporary_fd)
            if temporary_name is not None and watch_fd is not None:
                try:
                    self._remove_bounded_at(watch_fd, temporary_name)
                except Exception:
                    pass
            self._close_fds(entry_fd, watch_fd, root_fd)

    def _load_verified_fd(
        self, entry_fd: int, active_hash: bytes
    ) -> Optional[CachedPreview]:
        try:
            active_hash = self._hash(active_hash)
            encoded = self._read_bounded_at(
                entry_fd, PACKAGE_NAME, MAX_PACKAGE_BYTES
            )
            metadata_bytes = self._read_bounded_at(
                entry_fd, METADATA_NAME, MAX_METADATA_BYTES
            )
            package = parse_package(encoded)
            if package.sha256 != active_hash:
                return None
            metadata = json.loads(metadata_bytes.decode("utf-8"))
            if metadata != self._metadata(package):
                return None
            return CachedPreview(
                package,
                render_rgba_frames(package),
                tuple(frame.duration_ms for frame in package.frames),
            )
        except (
            OSError,
            ValueError,
            TypeError,
            json.JSONDecodeError,
            PetCacheError,
        ):
            return None

    @staticmethod
    def _watch_id(value: bytes) -> bytes:
        if not isinstance(value, bytes) or len(value) != 16 or not any(value):
            raise PetCacheError("watch identity is invalid")
        return value

    @staticmethod
    def _hash(value: bytes) -> bytes:
        if not isinstance(value, bytes) or len(value) != 32 or not any(value):
            raise PetCacheError("pet package hash is invalid")
        return value

    @staticmethod
    def _canonical(package: PetPackage) -> PetPackage:
        if not isinstance(package, PetPackage):
            raise PetCacheError("pet package is invalid")
        try:
            parsed = parse_package(package.encoded)
        except Exception as error:
            raise PetCacheError("pet package is invalid") from error
        if parsed != package:
            raise PetCacheError("pet package is not canonical")
        return parsed

    @staticmethod
    def _metadata(package: PetPackage) -> dict:
        return {
            "durations_ms": [frame.duration_ms for frame in package.frames],
            "frame_count": len(package.frames),
            "height": package.height,
            "schema": SCHEMA_VERSION,
            "sha256": package.sha256.hex(),
            "width": package.width,
        }

    def _open_root(self, *, create: bool) -> Optional[int]:
        absolute = pathlib.Path(os.path.abspath(os.fspath(self._root)))
        flags = self._directory_open_flags()
        descriptor = os.open(os.path.sep, flags)
        try:
            for component in absolute.parts[1:]:
                try:
                    child = self._open_directory_at(descriptor, component)
                except FileNotFoundError:
                    if not create:
                        os.close(descriptor)
                        return None
                    os.mkdir(component, mode=0o700, dir_fd=descriptor)
                    child = self._open_directory_at(descriptor, component)
                os.close(descriptor)
                descriptor = child
            return descriptor
        except (OSError, PetCacheError) as error:
            os.close(descriptor)
            if not create:
                return None
            if isinstance(error, PetCacheError):
                raise
            raise PetCacheError(
                "cache root chain must contain only real directories"
            ) from error

    @classmethod
    def _open_directory_at(cls, parent_fd: int, name: str) -> int:
        descriptor = os.open(
            name, cls._directory_open_flags(), dir_fd=parent_fd
        )
        if not stat.S_ISDIR(os.fstat(descriptor).st_mode):
            os.close(descriptor)
            raise PetCacheError("cache directory is invalid")
        return descriptor

    @classmethod
    def _open_or_create_directory_at(cls, parent_fd: int, name: str) -> int:
        try:
            return cls._open_directory_at(parent_fd, name)
        except FileNotFoundError:
            os.mkdir(name, mode=0o700, dir_fd=parent_fd)
            return cls._open_directory_at(parent_fd, name)

    @staticmethod
    def _directory_open_flags() -> int:
        return (
            os.O_RDONLY
            | getattr(os, "O_DIRECTORY", 0)
            | getattr(os, "O_NOFOLLOW", 0)
        )

    @classmethod
    def _create_temporary_directory(
        cls, parent_fd: int, package_hash: bytes
    ) -> tuple[str, int]:
        prefix = ".{}-".format(package_hash.hex())
        for _ in range(16):
            name = "{}{}.tmp".format(prefix, secrets.token_hex(8))
            try:
                os.mkdir(name, mode=0o700, dir_fd=parent_fd)
            except FileExistsError:
                continue
            return name, cls._open_directory_at(parent_fd, name)
        raise PetCacheError("cache temporary name allocation failed")

    @staticmethod
    def _write_fsynced_at(
        parent_fd: int, name: str, payload: bytes
    ) -> None:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
        flags |= getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(name, flags, 0o600, dir_fd=parent_fd)
        try:
            if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                raise PetCacheError("cache leaf must be a regular file")
            view = memoryview(payload)
            while view:
                written = os.write(descriptor, view)
                if written <= 0:
                    raise PetCacheError("cache write did not advance")
                view = view[written:]
            os.fsync(descriptor)
        finally:
            os.close(descriptor)

    @staticmethod
    def _read_bounded_at(
        parent_fd: int, name: str, maximum: int
    ) -> bytes:
        before = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        if not stat.S_ISREG(before.st_mode):
            raise PetCacheError("cache leaf must be a regular file")
        flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(name, flags, dir_fd=parent_fd)
        try:
            opened = os.fstat(descriptor)
            if (
                not stat.S_ISREG(opened.st_mode)
                or opened.st_dev != before.st_dev
                or opened.st_ino != before.st_ino
            ):
                raise PetCacheError("cache leaf changed during validation")
            chunks = []
            remaining = maximum + 1
            while remaining:
                chunk = os.read(descriptor, remaining)
                if not chunk:
                    break
                chunks.append(chunk)
                remaining -= len(chunk)
            payload = b"".join(chunks)
        finally:
            os.close(descriptor)
        if len(payload) > maximum:
            raise PetCacheError("cache file exceeds its bound")
        return payload

    @staticmethod
    def _entry_exists(parent_fd: int, name: str) -> bool:
        try:
            os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
            return True
        except FileNotFoundError:
            return False

    def _remove_bounded_at(self, parent_fd: int, name: str) -> None:
        counters = [0, 0]
        self._remove_entry_at(parent_fd, name, 0, counters)

    def _remove_entry_at(
        self,
        parent_fd: int,
        name: str,
        depth: int,
        counters: list[int],
    ) -> None:
        if counters[0] >= MAX_REMOVAL_NODES or depth > MAX_REMOVAL_DEPTH:
            raise PetCacheError("cache deletion exceeds its bound")
        name, info = self._capture_for_removal(parent_fd, name)
        counters[0] += 1
        if stat.S_ISREG(info.st_mode):
            counters[1] += info.st_size
            if counters[1] > MAX_REMOVAL_BYTES:
                raise PetCacheError("cache deletion exceeds its byte bound")
        if not stat.S_ISDIR(info.st_mode):
            name, _ = self._capture_for_removal(
                parent_fd, name, expected=info
            )
            os.unlink(name, dir_fd=parent_fd)
            return

        child_fd = self._open_directory_at(parent_fd, name)
        try:
            opened = os.fstat(child_fd)
            if opened.st_dev != info.st_dev or opened.st_ino != info.st_ino:
                raise PetCacheError("cache directory changed during deletion")
            with os.scandir(child_fd) as entries:
                children = [entry.name for entry in entries]
            for child_name in children:
                self._remove_entry_at(
                    child_fd, child_name, depth + 1, counters
                )
        finally:
            os.close(child_fd)

    @classmethod
    def _capture_for_removal(
        cls,
        parent_fd: int,
        name: str,
        expected: Optional[os.stat_result] = None,
    ) -> tuple[str, os.stat_result]:
        before = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        if expected is not None and (
            before.st_dev != expected.st_dev
            or before.st_ino != expected.st_ino
        ):
            raise PetCacheError("cache entry changed during deletion")
        with os.scandir(parent_fd) as entries:
            quarantine_count = sum(
                entry.name.startswith(".remove-") for entry in entries
            )
        if quarantine_count >= MAX_REMOVAL_QUARANTINES:
            raise PetCacheError("cache removal name allocation failed")
        for _ in range(16):
            captured_name = ".remove-{}.tmp".format(secrets.token_hex(16))
            if cls._entry_exists(parent_fd, captured_name):
                continue
            os.rename(
                name,
                captured_name,
                src_dir_fd=parent_fd,
                dst_dir_fd=parent_fd,
            )
            captured = os.stat(
                captured_name,
                dir_fd=parent_fd,
                follow_symlinks=False,
            )
            if (
                captured.st_dev != before.st_dev
                or captured.st_ino != before.st_ino
            ):
                raise PetCacheError("cache entry changed during deletion")
            return captured_name, captured
        raise PetCacheError("cache removal name allocation failed")

    @staticmethod
    def _close_fds(*descriptors: Optional[int]) -> None:
        for descriptor in descriptors:
            if descriptor is not None:
                os.close(descriptor)
