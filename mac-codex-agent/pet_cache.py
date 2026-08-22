"""Authoritative watch-identity/hash keyed cache for canonical pet previews."""

from __future__ import annotations

import dataclasses
import json
import os
import pathlib
import stat
import tempfile
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
        self._publish_atomic(
            self._cache_path(watch_id, canonical.sha256),
            canonical.encoded,
        )

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
        return self._load_verified(
            self._cache_path(watch_id, active_hash), active_hash
        )

    def remove_watch(self, watch_id: bytes) -> None:
        path = self._watch_path(self._watch_id(watch_id))
        if not self._real_directory(self._root):
            return
        if path.exists() or path.is_symlink():
            self._remove_bounded(path)
            if self._real_directory(self._root):
                self._fsync_directory(self._root)

    def _publish_atomic(
        self, destination: pathlib.Path, encoded: bytes
    ) -> None:
        try:
            watch_id = self._watch_id(bytes.fromhex(destination.parent.name))
            package = parse_package(encoded)
        except Exception as error:
            raise PetCacheError("cache publication is invalid") from error
        expected = self._cache_path(watch_id, package.sha256)
        if destination != expected:
            raise PetCacheError("cache destination is not canonical")
        if self._root.exists() or self._root.is_symlink():
            if not self._real_directory(self._root):
                raise PetCacheError("cache root must be a real directory")
        else:
            self._root.mkdir(parents=True, exist_ok=False)
        if not self._real_directory(self._root):
            raise PetCacheError("cache root must not be a symlink")
        watch_path = self._watch_path(watch_id)
        if watch_path.exists() or watch_path.is_symlink():
            if not self._real_directory(watch_path):
                raise PetCacheError("watch cache must be a real directory")
        else:
            watch_path.mkdir(exist_ok=False)
        if not self._real_directory(watch_path):
            raise PetCacheError("watch cache must not be a symlink")

        existing = self._load_verified(destination, package.sha256)
        if existing is not None:
            return
        if destination.exists() or destination.is_symlink():
            self._remove_bounded(destination)

        temporary = pathlib.Path(
            tempfile.mkdtemp(
                prefix=".{}-".format(package.sha256.hex()),
                suffix=".tmp",
                dir=str(watch_path),
            )
        )
        try:
            self._write_fsynced(temporary / PACKAGE_NAME, package.encoded)
            self._write_fsynced(
                temporary / METADATA_NAME,
                json.dumps(
                    self._metadata(package),
                    sort_keys=True,
                    separators=(",", ":"),
                ).encode("utf-8"),
            )
            self._fsync_directory(temporary)
            os.rename(temporary, destination)
            self._fsync_directory(watch_path)
        except Exception:
            if temporary.exists() or temporary.is_symlink():
                try:
                    self._remove_bounded(temporary)
                except Exception:
                    pass
            raise

    def _load_verified(
        self, path: pathlib.Path, active_hash: bytes
    ) -> Optional[CachedPreview]:
        try:
            watch_id = self._watch_id(bytes.fromhex(path.parent.name))
            active_hash = self._hash(active_hash)
            if (
                path != self._cache_path(watch_id, active_hash)
                or not self._real_directory(self._root)
                or not self._real_directory(path.parent)
                or not self._real_directory(path)
            ):
                return None
            package_path = path / PACKAGE_NAME
            metadata_path = path / METADATA_NAME
            if package_path.is_symlink() or metadata_path.is_symlink():
                return None
            encoded = self._read_bounded(package_path, MAX_PACKAGE_BYTES)
            metadata_bytes = self._read_bounded(
                metadata_path, MAX_METADATA_BYTES
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

    def _cache_path(
        self, watch_id: bytes, package_hash: bytes
    ) -> pathlib.Path:
        return self._watch_path(self._watch_id(watch_id)) / self._hash(
            package_hash
        ).hex()

    def _watch_path(self, watch_id: bytes) -> pathlib.Path:
        return self._root / self._watch_id(watch_id).hex()

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

    @staticmethod
    def _write_fsynced(path: pathlib.Path, payload: bytes) -> None:
        with path.open("xb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())

    @staticmethod
    def _fsync_directory(path: pathlib.Path) -> None:
        flags = os.O_RDONLY
        flags |= getattr(os, "O_DIRECTORY", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(str(path), flags)
        try:
            if not stat.S_ISDIR(os.fstat(descriptor).st_mode):
                raise PetCacheError("cache directory is invalid")
            os.fsync(descriptor)
        finally:
            os.close(descriptor)

    @staticmethod
    def _read_bounded(path: pathlib.Path, maximum: int) -> bytes:
        before = path.lstat()
        if not stat.S_ISREG(before.st_mode):
            raise PetCacheError("cache leaf must be a regular file")
        flags = os.O_RDONLY | getattr(os, "O_NONBLOCK", 0)
        flags |= getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(str(path), flags)
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
    def _real_directory(path: pathlib.Path) -> bool:
        try:
            return stat.S_ISDIR(path.lstat().st_mode)
        except OSError:
            return False

    def _remove_bounded(self, path: pathlib.Path) -> None:
        root = pathlib.Path(path)
        pending = [(root, 0)]
        nodes = []
        total_bytes = 0
        while pending:
            current, depth = pending.pop()
            if len(nodes) >= MAX_REMOVAL_NODES or depth > MAX_REMOVAL_DEPTH:
                raise PetCacheError("cache deletion exceeds its bound")
            info = current.lstat()
            nodes.append((current, info.st_mode))
            if stat.S_ISREG(info.st_mode):
                total_bytes += info.st_size
                if total_bytes > MAX_REMOVAL_BYTES:
                    raise PetCacheError("cache deletion exceeds its byte bound")
            elif stat.S_ISDIR(info.st_mode):
                with os.scandir(current) as entries:
                    children = [pathlib.Path(entry.path) for entry in entries]
                pending.extend((child, depth + 1) for child in children)
        for current, mode in reversed(nodes):
            if stat.S_ISDIR(mode):
                current.rmdir()
            else:
                current.unlink()
