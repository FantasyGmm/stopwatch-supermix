"""Reproducible source dependency bootstrap support for StopWatch."""

import dataclasses
import hashlib
import json
import os
import pathlib
import re
import shutil
import stat
import struct
import subprocess
import tempfile
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


@dataclasses.dataclass(frozen=True)
class VerificationIssue:
    path: pathlib.Path
    category: str
    message: str


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


def _sha256_file(path):
    hasher = hashlib.sha256()
    with pathlib.Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def run_git(*args, cwd=None):
    completed = subprocess.run(
        ["git", "-c", "gc.auto=0", "-c", "maintenance.auto=false", *args],
        cwd=cwd,
        text=True,
        capture_output=True,
    )
    if completed.returncode:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise DependencyError("git {} failed: {}".format(" ".join(args), detail))
    return completed.stdout.rstrip()


def _submodule_revisions(path):
    output = run_git("submodule", "status", "--recursive", cwd=path)
    revisions = {}
    for line in output.splitlines():
        if not line:
            continue
        state = line[0]
        fields = line[1:].split()
        if len(fields) < 2 or state != " ":
            raise DependencyError("submodule is not at its recorded revision: {}".format(line))
        revisions[fields[1]] = fields[0]
    return dict(sorted(revisions.items()))


def _stamp_payload(spec, submodules, content_sha256):
    return {
        "schema": 1,
        "url": spec.url,
        "revision": spec.revision,
        "release": spec.release,
        "with_submodules": spec.with_submodules,
        "submodules": submodules,
        "patch_sha256": spec.patch.sha256 if spec.patch else None,
        "content_sha256": content_sha256,
    }


def stage_component(spec, destination):
    """Materialize and validate one dependency in a new staging directory."""

    destination = pathlib.Path(destination)
    if destination.exists():
        raise DependencyError("staging destination already exists: {}".format(destination))

    if spec.patch:
        actual_patch_hash = _sha256_file(spec.patch.path)
        if actual_patch_hash != spec.patch.sha256:
            raise DependencyError(
                "patch hash mismatch for {}: expected {}, got {}".format(
                    spec.patch.path, spec.patch.sha256, actual_patch_hash
                )
            )

    run_git(
        "clone",
        "--quiet",
        "--no-checkout",
        "--filter=blob:none",
        spec.url,
        str(destination),
    )
    run_git("checkout", "--quiet", "--detach", spec.revision, cwd=destination)
    if run_git("rev-parse", "HEAD", cwd=destination) != spec.revision:
        raise DependencyError("checkout did not resolve to {}".format(spec.revision))

    if spec.with_submodules:
        run_git("submodule", "update", "--init", "--recursive", cwd=destination)

    if spec.patch:
        run_git("apply", "--check", str(spec.patch.path), cwd=destination)
        run_git("apply", str(spec.patch.path), cwd=destination)

    submodules = _submodule_revisions(destination) if spec.with_submodules else {}
    content_sha256 = content_tree_hash(destination)
    write_stamp(destination, _stamp_payload(spec, submodules, content_sha256))
    return destination


def _read_stamp(path):
    stamp_path = pathlib.Path(path) / STAMP_NAME
    if not stamp_path.is_file():
        return None
    try:
        stamp = json.loads(stamp_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return stamp if isinstance(stamp, dict) else None


def verify_component(spec):
    path = spec.path
    if not path.is_dir():
        return VerificationIssue(path, "missing", "dependency directory is absent")

    stamp = _read_stamp(path)
    if stamp is None:
        return VerificationIssue(path, "missing_stamp", "dependency stamp is absent or invalid")

    expected_fields = {
        "schema": 1,
        "url": spec.url,
        "revision": spec.revision,
        "release": spec.release,
        "with_submodules": spec.with_submodules,
        "patch_sha256": spec.patch.sha256 if spec.patch else None,
    }
    for field, expected in expected_fields.items():
        if stamp.get(field) != expected:
            category = "wrong_revision" if field == "revision" else "stamp_mismatch"
            return VerificationIssue(
                path,
                category,
                "stamp {} is {!r}, expected {!r}".format(field, stamp.get(field), expected),
            )

    if spec.patch:
        actual_patch_hash = _sha256_file(spec.patch.path)
        if actual_patch_hash != spec.patch.sha256:
            return VerificationIssue(
                path,
                "wrong_patch",
                "patch hash is {}, expected {}".format(
                    actual_patch_hash, spec.patch.sha256
                ),
            )

    try:
        remote_url = run_git("remote", "get-url", "origin", cwd=path)
        if remote_url != spec.url:
            return VerificationIssue(
                path, "wrong_url", "origin is {}, expected {}".format(remote_url, spec.url)
            )
        head = run_git("rev-parse", "HEAD", cwd=path)
        if head != spec.revision:
            return VerificationIssue(
                path,
                "wrong_revision",
                "HEAD is {}, expected {}".format(head, spec.revision),
            )
        submodules = _submodule_revisions(path) if spec.with_submodules else {}
    except DependencyError as error:
        return VerificationIssue(path, "git_error", str(error))

    if stamp.get("submodules") != submodules:
        return VerificationIssue(
            path,
            "wrong_submodules",
            "submodule revisions do not match the dependency stamp",
        )

    actual_content = content_tree_hash(path)
    if stamp.get("content_sha256") != actual_content:
        return VerificationIssue(
            path,
            "content_changed",
            "local changes detected: content hash is {}, expected {}".format(
                actual_content, stamp.get("content_sha256")
            ),
        )
    return None


def verify_all(specs, root=None):
    del root
    issues = []
    for spec in specs:
        issue = verify_component(spec)
        if issue is not None:
            issues.append(issue)
    return issues


def _remove_path(path):
    path = pathlib.Path(path)
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.exists():
        shutil.rmtree(str(path))


def materialize_all(specs, root, force=False):
    """Stage all required dependencies before replacing any destination."""

    project_root = pathlib.Path(root).resolve()
    project_root.mkdir(parents=True, exist_ok=True)
    requested = []
    for spec in specs:
        if spec.path.exists():
            issue = verify_component(spec)
            if issue is None:
                continue
            if not force:
                message = issue.message
                if issue.category != "content_changed":
                    message = "{}; use --force to replace it".format(message)
                raise DependencyError("{}: {}".format(spec.path, message))
        requested.append(spec)

    if not requested:
        return

    with tempfile.TemporaryDirectory(
        prefix=".stopwatch-dependencies-", dir=str(project_root)
    ) as temporary:
        temporary_root = pathlib.Path(temporary)
        staged_root = temporary_root / "staged"
        backup_root = temporary_root / "backups"
        staged_root.mkdir()
        backup_root.mkdir()

        staged = []
        for index, spec in enumerate(requested):
            staged_path = staged_root / "{:03d}".format(index)
            stage_component(spec, staged_path)
            staged.append((spec, staged_path, backup_root / "{:03d}".format(index)))

        replaced = []
        try:
            for spec, staged_path, backup_path in staged:
                spec.path.parent.mkdir(parents=True, exist_ok=True)
                had_existing = spec.path.exists() or spec.path.is_symlink()
                if had_existing:
                    os.replace(str(spec.path), str(backup_path))
                try:
                    os.replace(str(staged_path), str(spec.path))
                except Exception:
                    if had_existing and backup_path.exists():
                        os.replace(str(backup_path), str(spec.path))
                    raise
                replaced.append((spec.path, backup_path, had_existing))
        except Exception as error:
            for destination, backup_path, had_existing in reversed(replaced):
                _remove_path(destination)
                if had_existing and backup_path.exists():
                    os.replace(str(backup_path), str(destination))
            if isinstance(error, DependencyError):
                raise
            raise DependencyError("cannot replace dependencies: {}".format(error)) from error
