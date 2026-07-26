#!/usr/bin/env python3
"""Provision one StopWatch voice key without exposing it on command lines."""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import secrets
import stat
import subprocess
import sys
from collections.abc import Callable


KEYCHAIN_SERVICE = "com.stopwatch.codex-agent.voice"
KEYCHAIN_ACCOUNT = "StopWatch-Codex"
KEY_FILE_ENV = "STOPWATCH_VOICE_KEY_FILE"
SECURITY_COMMAND_ENV = "STOPWATCH_SECURITY_COMMAND"
KEY_PATTERN = re.compile(r"[0-9a-f]{32}\n")
KEYCHAIN_TIMEOUT_SECONDS = 10.0
MAX_KEY_FILE_BYTES = 33


class ProvisioningError(RuntimeError):
    """Raised when key provisioning cannot complete safely."""


def default_key_path(home: pathlib.Path | None = None) -> pathlib.Path:
    if override := os.environ.get(KEY_FILE_ENV):
        return pathlib.Path(override).expanduser()
    base = pathlib.Path.home() if home is None else home
    return base / "Library/Application Support/StopWatch Codex Agent/device-key.hex"


def _descriptor_flags(base: int) -> int:
    flags = base
    if hasattr(os, "O_CLOEXEC"):
        flags |= os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    return flags


def _open_trusted_parent(path: pathlib.Path, *, create: bool = False) -> int:
    if create:
        try:
            path.mkdir(mode=0o700, parents=True, exist_ok=True)
        except OSError:
            raise ProvisioningError("voice key parent cannot be created") from None
    try:
        metadata = path.lstat()
    except FileNotFoundError:
        raise ProvisioningError("voice key parent is missing") from None
    if not stat.S_ISDIR(metadata.st_mode) or path.is_symlink():
        raise ProvisioningError("voice key parent is not a trusted directory")
    if hasattr(os, "geteuid") and metadata.st_uid != os.geteuid():
        raise ProvisioningError("voice key parent owner is invalid")
    if stat.S_IMODE(metadata.st_mode) & 0o022:
        raise ProvisioningError("voice key parent permissions are unsafe")
    flags = _descriptor_flags(os.O_RDONLY)
    if hasattr(os, "O_DIRECTORY"):
        flags |= os.O_DIRECTORY
    try:
        descriptor = os.open(path, flags)
    except OSError:
        raise ProvisioningError("voice key parent cannot be opened safely") from None
    opened = os.fstat(descriptor)
    if (
        not stat.S_ISDIR(opened.st_mode)
        or opened.st_dev != metadata.st_dev
        or opened.st_ino != metadata.st_ino
    ):
        os.close(descriptor)
        raise ProvisioningError("voice key parent changed during validation")
    return descriptor


def _validate_private_key_descriptor(descriptor: int) -> None:
    metadata = os.fstat(descriptor)
    if not stat.S_ISREG(metadata.st_mode):
        raise ProvisioningError("voice key path is not a regular file")
    if stat.S_IMODE(metadata.st_mode) != 0o600:
        raise ProvisioningError("voice key file permissions must be 0600")
    if hasattr(os, "geteuid") and metadata.st_uid != os.geteuid():
        raise ProvisioningError("voice key file owner is invalid")


def read_key_file(path: pathlib.Path) -> str:
    path = path.expanduser()
    try:
        parent_descriptor = _open_trusted_parent(path.parent)
    except ProvisioningError as error:
        if "missing" in str(error):
            raise ProvisioningError("voice key file is missing") from None
        raise
    try:
        try:
            descriptor = os.open(
                path.name,
                _descriptor_flags(os.O_RDONLY),
                dir_fd=parent_descriptor,
            )
        except FileNotFoundError:
            raise ProvisioningError("voice key file is missing") from None
        except OSError:
            raise ProvisioningError("voice key path is not a regular file") from None
        try:
            _validate_private_key_descriptor(descriptor)
            raw = os.read(descriptor, MAX_KEY_FILE_BYTES + 1)
        except OSError:
            raise ProvisioningError("voice key file cannot be read") from None
        finally:
            os.close(descriptor)
    finally:
        os.close(parent_descriptor)
    try:
        encoded = raw.decode("ascii")
    except UnicodeDecodeError:
        raise ProvisioningError("voice key file is malformed") from None
    if KEY_PATTERN.fullmatch(encoded) is None:
        raise ProvisioningError("voice key file is malformed")
    return encoded[:-1]


def ensure_key_file(path: pathlib.Path) -> str:
    path = path.expanduser()
    try:
        return read_key_file(path)
    except ProvisioningError as error:
        if "missing" not in str(error):
            raise

    encoded = secrets.token_hex(16)
    parent_descriptor = _open_trusted_parent(path.parent, create=True)
    flags = _descriptor_flags(os.O_WRONLY | os.O_CREAT | os.O_EXCL)
    try:
        try:
            descriptor = os.open(path.name, flags, 0o600, dir_fd=parent_descriptor)
        except FileExistsError:
            return read_key_file(path)
        try:
            os.fchmod(descriptor, 0o600)
            with os.fdopen(descriptor, "wb") as stream:
                stream.write((encoded + "\n").encode("ascii"))
                stream.flush()
                os.fsync(stream.fileno())
            os.fsync(parent_descriptor)
        except Exception:
            try:
                os.close(descriptor)
            except OSError:
                pass
            try:
                os.unlink(path.name, dir_fd=parent_descriptor)
            except OSError:
                pass
            raise
        return encoded
    finally:
        os.close(parent_descriptor)


def import_keychain(
    encoded_key: str,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> None:
    if re.fullmatch(r"[0-9a-f]{32}", encoded_key) is None:
        raise ProvisioningError("voice key is malformed")
    security_command = os.environ.get(SECURITY_COMMAND_ENV, "security")
    command = [
        security_command,
        "add-generic-password",
        "-U",
        "-s",
        KEYCHAIN_SERVICE,
        "-a",
        KEYCHAIN_ACCOUNT,
        "-w",
    ]
    try:
        result = runner(
            command,
            input=encoded_key + "\n",
            capture_output=True,
            text=True,
            check=False,
            timeout=KEYCHAIN_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.TimeoutExpired):
        raise ProvisioningError("voice key could not be imported into Keychain") from None
    if result.returncode != 0:
        raise ProvisioningError("voice key could not be imported into Keychain")


def _header_contents(encoded_key: str) -> str:
    key_bytes = bytes.fromhex(encoded_key)
    rows = []
    for offset in range(0, len(key_bytes), 8):
        values = ", ".join(f"0x{value:02x}" for value in key_bytes[offset : offset + 8])
        rows.append(f"    {values},")
    return (
        "#pragma once\n\n"
        "#include <array>\n"
        "#include <cstdint>\n\n"
        "namespace voice_input::generated {\n"
        "inline constexpr std::array<uint8_t, 16> kDeviceKey = {\n"
        + "\n".join(rows)
        + "\n};\n"
        "}  // namespace voice_input::generated\n"
    )


def generate_firmware_header(key_path: pathlib.Path, output: pathlib.Path) -> None:
    encoded_key = read_key_file(key_path.expanduser())
    output = output.expanduser()
    parent_descriptor = _open_trusted_parent(output.parent, create=True)
    temporary_name = f".{output.name}.{secrets.token_hex(8)}"
    descriptor = -1
    try:
        try:
            existing = os.stat(
                output.name, dir_fd=parent_descriptor, follow_symlinks=False
            )
        except FileNotFoundError:
            existing = None
        except OSError:
            raise ProvisioningError("firmware header output path is unsafe") from None
        if existing is not None and not stat.S_ISREG(existing.st_mode):
            raise ProvisioningError("firmware header output path is unsafe")
        descriptor = os.open(
            temporary_name,
            _descriptor_flags(os.O_WRONLY | os.O_CREAT | os.O_EXCL),
            0o600,
            dir_fd=parent_descriptor,
        )
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="ascii", newline="\n") as stream:
            stream.write(_header_contents(encoded_key))
            stream.flush()
            os.fsync(stream.fileno())
        descriptor = -1
        os.replace(
            temporary_name,
            output.name,
            src_dir_fd=parent_descriptor,
            dst_dir_fd=parent_descriptor,
        )
        os.fsync(parent_descriptor)
    except Exception:
        if descriptor >= 0:
            try:
                os.close(descriptor)
            except OSError:
                pass
        try:
            os.unlink(temporary_name, dir_fd=parent_descriptor)
        except OSError:
            pass
        raise
    finally:
        os.close(parent_descriptor)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    provision = subparsers.add_parser("provision")
    provision.add_argument("--firmware-header", type=pathlib.Path)
    firmware = subparsers.add_parser("firmware-header")
    firmware.add_argument("--output", required=True, type=pathlib.Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    key_path = default_key_path()
    try:
        if arguments.command == "provision":
            encoded_key = ensure_key_file(key_path)
            import_keychain(encoded_key)
            if arguments.firmware_header is not None:
                generate_firmware_header(key_path, arguments.firmware_header)
        else:
            generate_firmware_header(key_path, arguments.output)
    except ProvisioningError as error:
        print(f"Voice key provisioning failed: {error}", file=sys.stderr)
        return 2
    print("StopWatch voice key provisioning completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
