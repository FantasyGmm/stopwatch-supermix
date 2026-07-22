"""Reproducible source dependency bootstrap support for StopWatch."""

import dataclasses
import hashlib
import json
import os
import pathlib
import re
import stat
import struct
import urllib.parse
from typing import Iterable, Optional, Sequence, Tuple


STAMP_NAME = ".stopwatch-dependency.json"
_REVISION_PATTERN = re.compile(r"[0-9a-f]{40}")
_SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
_REPO_KEYS = {
    "url",
    "path",
    "revision",
    "release",
    "with_submodules",
    "patch",
}
_REQUIRED_REPO_KEYS = {"url", "path", "revision", "with_submodules"}
_PATCH_KEYS = {"path", "sha256"}


class DependencyError(RuntimeError):
    """Raised when dependency configuration or materialization is unsafe."""


@dataclasses.dataclass(frozen=True)
class PatchSpec:
    path: pathlib.Path
    sha256: str


@dataclasses.dataclass(frozen=True)
class RepoSpec:
    url: str
    path: pathlib.Path
    revision: str
    release: Optional[str]
    with_submodules: bool
    patch: Optional[PatchSpec]


def _require_mapping(value, label):
    if not isinstance(value, dict):
        raise DependencyError("{} must be an object".format(label))
    return value


def _resolve_inside(root: pathlib.Path, relative, allowed_root: pathlib.Path, label):
    if not isinstance(relative, str) or not relative:
        raise DependencyError("{} must be a non-empty string".format(label))
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(allowed_root)
    except ValueError as error:
        raise DependencyError("{} escapes {}".format(label, allowed_root)) from error
    if candidate == allowed_root:
        raise DependencyError("{} must name a child path".format(label))
    return candidate


def parse_manifest(
    raw_entries,
    root,
    allowed_schemes: Sequence[str] = ("https",),
):
    """Validate all manifest entries and return immutable repository specs."""

    project_root = pathlib.Path(root).resolve()
    components_root = (project_root / "components").resolve()
    patches_root = (project_root / "patches").resolve()
    if not isinstance(raw_entries, list):
        raise DependencyError("repos.json must contain a list")

    specs = []
    seen_paths = set()
    for index, raw_entry in enumerate(raw_entries):
        entry = _require_mapping(raw_entry, "repository entry {}".format(index))
        keys = set(entry)
        unknown = keys - _REPO_KEYS
        missing = _REQUIRED_REPO_KEYS - keys
        if unknown:
            raise DependencyError(
                "repository entry {} has unknown keys: {}".format(
                    index, ", ".join(sorted(unknown))
                )
            )
        if missing:
            raise DependencyError(
                "repository entry {} is missing keys: {}".format(
                    index, ", ".join(sorted(missing))
                )
            )

        url = entry["url"]
        if not isinstance(url, str):
            raise DependencyError("repository URL must be a string")
        parsed_url = urllib.parse.urlparse(url)
        if parsed_url.scheme not in allowed_schemes or not parsed_url.netloc:
            raise DependencyError(
                "repository URL must use one of: {}".format(
                    ", ".join(allowed_schemes)
                )
            )

        path = _resolve_inside(
            project_root, entry["path"], components_root, "repository path"
        )
        if path in seen_paths:
            raise DependencyError("duplicate repository path: {}".format(path))
        seen_paths.add(path)

        revision = entry["revision"]
        if not isinstance(revision, str) or not _REVISION_PATTERN.fullmatch(revision):
            raise DependencyError("revision must be 40 lowercase hexadecimal characters")

        release = entry.get("release")
        if release is not None and (not isinstance(release, str) or not release):
            raise DependencyError("release must be a non-empty string when present")

        with_submodules = entry["with_submodules"]
        if not isinstance(with_submodules, bool):
            raise DependencyError("with_submodules must be a boolean")

        patch = None
        if entry.get("patch") is not None:
            patch_entry = _require_mapping(entry["patch"], "patch")
            patch_keys = set(patch_entry)
            if patch_keys != _PATCH_KEYS:
                raise DependencyError("patch must contain only path and sha256")
            patch_path = _resolve_inside(
                project_root, patch_entry["path"], patches_root, "patch path"
            )
            if not patch_path.is_file():
                raise DependencyError("patch file does not exist: {}".format(patch_path))
            patch_sha256 = patch_entry["sha256"]
            if not isinstance(patch_sha256, str) or not _SHA256_PATTERN.fullmatch(
                patch_sha256
            ):
                raise DependencyError(
                    "patch sha256 must be 64 lowercase hexadecimal characters"
                )
            patch = PatchSpec(path=patch_path, sha256=patch_sha256)

        specs.append(
            RepoSpec(
                url=url,
                path=path,
                revision=revision,
                release=release,
                with_submodules=with_submodules,
                patch=patch,
            )
        )
    return specs


def load_manifest(root):
    project_root = pathlib.Path(root).resolve()
    try:
        raw_entries = json.loads(
            (project_root / "repos.json").read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        raise DependencyError("cannot read repos.json: {}".format(error)) from error
    return parse_manifest(raw_entries, project_root)


def _hash_field(hasher, value):
    encoded = value if isinstance(value, bytes) else str(value).encode("utf-8")
    hasher.update(struct.pack(">Q", len(encoded)))
    hasher.update(encoded)


def _tree_entries(root: pathlib.Path) -> Iterable[Tuple[pathlib.Path, pathlib.Path]]:
    entries = []
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if ".git" in relative.parts:
            continue
        if relative == pathlib.Path(STAMP_NAME):
            continue
        entries.append((relative, path))
    return sorted(entries, key=lambda item: item[0].as_posix())


def content_tree_hash(root):
    """Hash dependency content independently of Git metadata and its stamp."""

    tree_root = pathlib.Path(root)
    if not tree_root.is_dir():
        raise DependencyError("dependency tree does not exist: {}".format(tree_root))

    hasher = hashlib.sha256()
    for relative, path in _tree_entries(tree_root):
        metadata = path.lstat()
        _hash_field(hasher, relative.as_posix())
        if stat.S_ISLNK(metadata.st_mode):
            _hash_field(hasher, "symlink")
            _hash_field(hasher, os.readlink(str(path)))
        elif stat.S_ISDIR(metadata.st_mode):
            _hash_field(hasher, "directory")
        elif stat.S_ISREG(metadata.st_mode):
            _hash_field(hasher, "file")
            _hash_field(hasher, "executable" if metadata.st_mode & 0o111 else "regular")
            _hash_field(hasher, path.read_bytes())
        else:
            raise DependencyError("unsupported dependency entry: {}".format(path))
    return hasher.hexdigest()


def write_stamp(root, payload):
    stamp_path = pathlib.Path(root) / STAMP_NAME
    stamp_path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
