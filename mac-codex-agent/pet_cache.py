"""Authoritative watch-identity/hash keyed cache for canonical pet previews."""

from __future__ import annotations

import dataclasses
import ctypes
import errno
import fcntl
import json
import os
import pathlib
import secrets
import stat
import threading
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
MAX_CACHE_ENTRIES_PER_WATCH = 8
MAX_CACHE_BYTES_PER_WATCH = MAX_CACHE_ENTRIES_PER_WATCH * MAX_PACKAGE_BYTES
MAX_CACHE_DIRECTORY_ENTRIES = (
    MAX_CACHE_ENTRIES_PER_WATCH + MAX_REMOVAL_QUARANTINES
)
DARWIN_RENAME_EXCL = 0x00000004
TRANSACTION_LOCK_STRIPES = 64
_TRANSACTION_LOCKS = tuple(
    threading.Lock() for _ in range(TRANSACTION_LOCK_STRIPES)
)


class PetCacheError(RuntimeError):
    """A local preview cache entry failed bounded validation."""


@dataclasses.dataclass(frozen=True)
class CachedPreview:
    package: PetPackage
    rgba_frames: tuple[bytes, ...]
    durations_ms: tuple[int, ...]


@dataclasses.dataclass(frozen=True)
class _VerifiedHistoryEntry:
    mtime_ns: int
    name: str
    package_hash: bytes
    encoded_bytes: int
    info: os.stat_result
    descriptor: int


@dataclasses.dataclass(frozen=True)
class _PinnedCacheEntry:
    name: str
    info: os.stat_result
    descriptor: Optional[int]


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
        watch_fd = None
        transaction_lock = None
        try:
            try:
                watch_fd = self._open_directory_at(root_fd, watch_name)
            except FileNotFoundError:
                return
            except (OSError, PetCacheError):
                if not self._entry_exists(root_fd, watch_name):
                    return
                self._remove_bounded_at(root_fd, watch_name)
                os.fsync(root_fd)
                return
            transaction_lock = self._lock_transaction(watch_fd)
            expected = os.fstat(watch_fd)
            current = os.stat(
                watch_name, dir_fd=root_fd, follow_symlinks=False
            )
            if not self._same_identity(current, expected):
                raise PetCacheError("cache watch changed before removal")
            self._remove_bounded_at(
                root_fd,
                watch_name,
                expected=expected,
                opened_descriptor=watch_fd,
            )
            os.fsync(root_fd)
        finally:
            try:
                if transaction_lock is not None and watch_fd is not None:
                    self._unlock_transaction(watch_fd, transaction_lock)
            finally:
                self._close_fds(watch_fd, root_fd)

    def _publish_atomic(
        self, watch_id: bytes, package: PetPackage
    ) -> None:
        root_fd = None
        watch_fd = None
        entry_fd = None
        temporary_fd = None
        temporary_name = None
        transaction_lock = None
        try:
            root_fd = self._open_root(create=True)
            watch_fd = self._open_or_create_directory_at(
                root_fd, watch_id.hex()
            )
            transaction_lock = self._lock_transaction(watch_fd)
            current_watch = os.stat(
                watch_id.hex(), dir_fd=root_fd, follow_symlinks=False
            )
            if not self._same_identity(current_watch, os.fstat(watch_fd)):
                raise PetCacheError("cache watch changed before publication")
            destination_name = package.sha256.hex()
            try:
                entry_fd = self._open_directory_at(
                    watch_fd, destination_name
                )
            except OSError:
                entry_fd = None
            if entry_fd is not None:
                existing = self._read_verified_package_fd(
                    entry_fd, package.sha256
                )
                os.close(entry_fd)
                entry_fd = None
                if existing is not None:
                    self._reserve_history_capacity(
                        watch_fd,
                        protected_hash=package.sha256,
                        incoming_bytes=0,
                        adding_entry=False,
                    )
                    return
            if self._entry_exists(watch_fd, destination_name):
                self._remove_bounded_at(watch_fd, destination_name)

            self._reserve_history_capacity(
                watch_fd,
                protected_hash=None,
                incoming_bytes=len(package.encoded),
                adding_entry=True,
            )

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
            try:
                if transaction_lock is not None and watch_fd is not None:
                    self._unlock_transaction(watch_fd, transaction_lock)
            finally:
                self._close_fds(entry_fd, watch_fd, root_fd)

    def _load_verified_fd(
        self, entry_fd: int, active_hash: bytes
    ) -> Optional[CachedPreview]:
        try:
            package = self._read_verified_package_fd(
                entry_fd, active_hash
            )
            if package is None:
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

    def _read_verified_package_fd(
        self, entry_fd: int, active_hash: bytes
    ) -> Optional[PetPackage]:
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
            return package
        except (
            OSError,
            ValueError,
            TypeError,
            json.JSONDecodeError,
            PetCacheError,
        ):
            return None

    def _reserve_history_capacity(
        self,
        watch_fd: int,
        *,
        protected_hash: Optional[bytes],
        incoming_bytes: int,
        adding_entry: bool,
    ) -> None:
        names = self._bounded_directory_names(
            watch_fd,
            MAX_CACHE_DIRECTORY_ENTRIES,
            "cache directory exceeds its entry bound",
        )
        if adding_entry and len(names) >= MAX_CACHE_DIRECTORY_ENTRIES:
            raise PetCacheError("cache directory exceeds its entry bound")

        history: list[_VerifiedHistoryEntry] = []
        stale: list[_PinnedCacheEntry] = []
        try:
            for name in names:
                package_hash = self._cache_package_hash(name)
                if (
                    package_hash is None
                    and not self._is_cache_temporary_name(name)
                ):
                    continue
                entry_fd: Optional[int] = None
                try:
                    info = os.stat(
                        name,
                        dir_fd=watch_fd,
                        follow_symlinks=False,
                    )
                    if stat.S_ISDIR(info.st_mode):
                        entry_fd = self._open_directory_at(watch_fd, name)
                        info = os.fstat(entry_fd)
                        package = (
                            self._read_verified_package_fd(
                                entry_fd, package_hash
                            )
                            if package_hash is not None
                            else None
                        )
                        if package is not None:
                            history.append(
                                _VerifiedHistoryEntry(
                                    info.st_mtime_ns,
                                    name,
                                    package.sha256,
                                    len(package.encoded),
                                    info,
                                    entry_fd,
                                )
                            )
                            entry_fd = None
                            continue
                    else:
                        entry_fd = self._open_removal_descriptor(
                            watch_fd, name, info
                        )
                    stale.append(
                        _PinnedCacheEntry(
                            name,
                            info,
                            entry_fd,
                        )
                    )
                    entry_fd = None
                except FileNotFoundError:
                    continue
                except (OSError, ValueError, TypeError, PetCacheError) as error:
                    raise PetCacheError(
                        "cache-owned entry validation failed"
                    ) from error
                finally:
                    if entry_fd is not None:
                        os.close(entry_fd)

            for item in tuple(stale):
                self._remove_bounded_at(
                    watch_fd,
                    item.name,
                    expected=item.info,
                    opened_descriptor=item.descriptor,
                )
                if item.descriptor is not None:
                    os.close(item.descriptor)
                stale.remove(item)

            maximum_entries = MAX_CACHE_ENTRIES_PER_WATCH - int(adding_entry)
            maximum_bytes = MAX_CACHE_BYTES_PER_WATCH - incoming_bytes
            total_bytes = sum(item.encoded_bytes for item in history)
            while (
                len(history) > maximum_entries
                or total_bytes > maximum_bytes
            ):
                candidates = [
                    item
                    for item in history
                    if item.package_hash != protected_hash
                ]
                if not candidates:
                    raise PetCacheError(
                        "cache history cannot retain current pet"
                    )
                victim = min(
                    candidates,
                    key=lambda item: (item.mtime_ns, item.name),
                )
                self._remove_bounded_at(
                    watch_fd,
                    victim.name,
                    expected=victim.info,
                    opened_descriptor=victim.descriptor,
                )
                os.close(victim.descriptor)
                history.remove(victim)
                total_bytes -= victim.encoded_bytes
        finally:
            for item in history:
                os.close(item.descriptor)
            for item in stale:
                if item.descriptor is not None:
                    os.close(item.descriptor)

    @staticmethod
    def _cache_package_hash(name: str) -> Optional[bytes]:
        if len(name) != 64 or name.lower() != name:
            return None
        try:
            value = bytes.fromhex(name)
        except ValueError:
            return None
        return value if len(value) == 32 else None

    @classmethod
    def _is_cache_temporary_name(cls, name: str) -> bool:
        if not name.startswith(".") or not name.endswith(".tmp"):
            return False
        body = name[1:-4]
        if len(body) != 81 or body[64] != "-":
            return False
        package_name = body[:64]
        token = body[65:]
        if cls._cache_package_hash(package_name) is None:
            return False
        try:
            token_bytes = bytes.fromhex(token)
        except ValueError:
            return False
        return token.lower() == token and len(token_bytes) == 8

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
            try:
                os.mkdir(name, mode=0o700, dir_fd=parent_fd)
            except FileExistsError:
                pass
            return cls._open_directory_at(parent_fd, name)

    @staticmethod
    def _lock_transaction(watch_fd: int) -> threading.Lock:
        info = os.fstat(watch_fd)
        lock = _TRANSACTION_LOCKS[
            (info.st_dev ^ info.st_ino) % TRANSACTION_LOCK_STRIPES
        ]
        lock.acquire()
        try:
            fcntl.flock(watch_fd, fcntl.LOCK_EX)
        except Exception:
            lock.release()
            raise
        return lock

    @staticmethod
    def _unlock_transaction(
        watch_fd: int, lock: threading.Lock
    ) -> None:
        try:
            fcntl.flock(watch_fd, fcntl.LOCK_UN)
        finally:
            lock.release()

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

    def _remove_bounded_at(
        self,
        parent_fd: int,
        name: str,
        *,
        expected: Optional[os.stat_result] = None,
        opened_descriptor: Optional[int] = None,
    ) -> None:
        counters = [0, 0]
        owns_descriptor = opened_descriptor is None
        _, info, descriptor = self._capture_for_removal(
            parent_fd,
            name,
            expected=expected,
            opened_descriptor=opened_descriptor,
        )
        try:
            self._retire_opened_entry(info, descriptor, 0, counters)
        finally:
            if owns_descriptor and descriptor is not None:
                os.close(descriptor)

    def _retire_entry_at(
        self,
        parent_fd: int,
        name: str,
        depth: int,
        counters: list[int],
    ) -> None:
        if counters[0] >= MAX_REMOVAL_NODES or depth > MAX_REMOVAL_DEPTH:
            raise PetCacheError("cache deletion exceeds its bound")
        info = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        descriptor = self._open_removal_descriptor(parent_fd, name, info)
        try:
            self._retire_opened_entry(info, descriptor, depth, counters)
        finally:
            if descriptor is not None:
                os.close(descriptor)

    def _retire_opened_entry(
        self,
        info: os.stat_result,
        descriptor: Optional[int],
        depth: int,
        counters: list[int],
    ) -> None:
        if counters[0] >= MAX_REMOVAL_NODES or depth > MAX_REMOVAL_DEPTH:
            raise PetCacheError("cache deletion exceeds its bound")
        counters[0] += 1
        if stat.S_ISREG(info.st_mode):
            counters[1] += info.st_size
            if counters[1] > MAX_REMOVAL_BYTES:
                raise PetCacheError("cache deletion exceeds its byte bound")
            if descriptor is None:
                raise PetCacheError("cache leaf descriptor is missing")
            os.ftruncate(descriptor, 0)
            os.fsync(descriptor)
            return
        if not stat.S_ISDIR(info.st_mode):
            return

        if descriptor is None:
            raise PetCacheError("cache directory descriptor is missing")
        with os.scandir(descriptor) as entries:
            for entry in entries:
                if counters[0] >= MAX_REMOVAL_NODES:
                    raise PetCacheError("cache deletion exceeds its bound")
                self._retire_entry_at(
                    descriptor, entry.name, depth + 1, counters
                )

    @classmethod
    def _capture_for_removal(
        cls,
        parent_fd: int,
        name: str,
        *,
        expected: Optional[os.stat_result] = None,
        opened_descriptor: Optional[int] = None,
    ) -> tuple[str, os.stat_result, Optional[int]]:
        before = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
        if expected is not None and not cls._same_identity(before, expected):
            raise PetCacheError("cache entry changed during deletion")
        owns_descriptor = opened_descriptor is None
        descriptor = opened_descriptor
        if descriptor is None:
            descriptor = cls._open_removal_descriptor(parent_fd, name, before)
        elif not cls._same_identity(os.fstat(descriptor), before):
            raise PetCacheError("cache entry changed during deletion")
        try:
            for slot in range(MAX_REMOVAL_QUARANTINES):
                captured_name = ".remove-{:02x}.tmp".format(slot)
                if cls._entry_exists(parent_fd, captured_name):
                    continue
                try:
                    cls._rename_no_replace_at(
                        parent_fd, name, captured_name
                    )
                except OSError as error:
                    if error.errno == errno.EEXIST:
                        continue
                    raise
                captured = os.stat(
                    captured_name,
                    dir_fd=parent_fd,
                    follow_symlinks=False,
                )
                if (
                    captured.st_dev != before.st_dev
                    or captured.st_ino != before.st_ino
                ):
                    raise PetCacheError(
                        "cache entry changed during deletion"
                    )
                return captured_name, captured, descriptor
        except Exception:
            if owns_descriptor and descriptor is not None:
                os.close(descriptor)
            raise
        if owns_descriptor and descriptor is not None:
            os.close(descriptor)
        raise PetCacheError("cache removal name allocation failed")

    @staticmethod
    def _rename_no_replace_at(
        parent_fd: int,
        source_name: str,
        destination_name: str,
    ) -> None:
        libc = ctypes.CDLL(None, use_errno=True)
        renameatx_np = getattr(libc, "renameatx_np", None)
        if renameatx_np is None:
            raise PetCacheError("atomic cache quarantine is unavailable")
        renameatx_np.argtypes = (
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_int,
            ctypes.c_char_p,
            ctypes.c_uint,
        )
        renameatx_np.restype = ctypes.c_int
        result = renameatx_np(
            parent_fd,
            os.fsencode(source_name),
            parent_fd,
            os.fsencode(destination_name),
            DARWIN_RENAME_EXCL,
        )
        if result != 0:
            error_number = ctypes.get_errno()
            raise OSError(
                error_number,
                os.strerror(error_number),
                destination_name,
            )

    @classmethod
    def _open_removal_descriptor(
        cls,
        parent_fd: int,
        name: str,
        expected: os.stat_result,
    ) -> Optional[int]:
        if stat.S_ISDIR(expected.st_mode):
            descriptor = cls._open_directory_at(parent_fd, name)
        elif stat.S_ISREG(expected.st_mode):
            flags = os.O_WRONLY | getattr(os, "O_NONBLOCK", 0)
            flags |= getattr(os, "O_NOFOLLOW", 0)
            descriptor = os.open(name, flags, dir_fd=parent_fd)
        else:
            return None
        opened = os.fstat(descriptor)
        if not cls._same_identity(opened, expected):
            os.close(descriptor)
            raise PetCacheError("cache entry changed during deletion")
        return descriptor

    @staticmethod
    def _same_identity(
        left: os.stat_result, right: os.stat_result
    ) -> bool:
        return (
            left.st_dev == right.st_dev
            and left.st_ino == right.st_ino
            and stat.S_IFMT(left.st_mode) == stat.S_IFMT(right.st_mode)
        )

    @staticmethod
    def _bounded_directory_names(
        descriptor: int,
        maximum: int,
        error_message: str,
    ) -> list[str]:
        names = []
        with os.scandir(descriptor) as entries:
            for entry in entries:
                if len(names) >= maximum:
                    raise PetCacheError(error_message)
                names.append(entry.name)
        return names

    @staticmethod
    def _close_fds(*descriptors: Optional[int]) -> None:
        for descriptor in descriptors:
            if descriptor is not None:
                os.close(descriptor)
