#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
VERSION_FILE="$ROOT_DIR/VERSION"
INSTALLER="$ROOT_DIR/install_launch_agent.sh"
ARTIFACT_ROOT="${STOPWATCH_ARTIFACT_ROOT:-$PROJECT_ROOT/artifacts/macos-agent}"
LATEST_DIR="$ARTIFACT_ROOT/latest"
RELEASE_BUILD_ROOT=""

is_release_version() {
    [[ "$1" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]
}

next_release_version() {
    local current="$1"
    local previous_fingerprint="$2"
    local fingerprint="$3"
    if ! is_release_version "$current"; then
        echo "Invalid Agent version: $current" >&2
        return 2
    fi
    if [[ -z "$previous_fingerprint" || "$previous_fingerprint" == "$fingerprint" ]]; then
        printf '%s\n' "$current"
        return
    fi

    local major=""
    local minor=""
    local patch=""
    IFS=. read -r major minor patch <<<"$current"
    printf '%s.%s.%s\n' "$major" "$minor" "$((10#$patch + 1))"
}

release_input_paths() {
    (
        cd "$PROJECT_ROOT"
        find mac-codex-agent -type f \
            ! -path '*/tests/*' \
            ! -path '*/.native/*' \
            ! -path '*/.driver-cache/*' \
            ! -path '*/__pycache__/*' \
            ! -name VERSION \
            ! -name README.md \
            \( -name '*.py' -o -name '*.sh' -o -name '*.txt' \
               -o -name '*.mm' -o -name '*.h' \) \
            -print | LC_ALL=C sort
    )
}

release_input_fingerprint() {
    local root="$1"
    shift
    python3 - "$root" "$@" <<'PY'
import hashlib
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
paths = sorted(sys.argv[2:])
if not paths:
    raise SystemExit("no release inputs")
digest = hashlib.sha256()
for relative in paths:
    path = (root / relative).resolve()
    if root != path and root not in path.parents:
        raise SystemExit("release input escapes root")
    digest.update(relative.encode("utf-8"))
    digest.update(b"\0")
    digest.update(path.read_bytes())
    digest.update(b"\0")
print(digest.hexdigest())
PY
}

release_metadata_field() {
    local metadata="$1"
    local field="$2"
    python3 - "$metadata" "$field" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
if not path.is_file():
    print("")
else:
    try:
        value = json.loads(path.read_text(encoding="utf-8")).get(sys.argv[2], "")
    except (OSError, ValueError, TypeError):
        value = ""
    print(value if isinstance(value, (str, int)) else "")
PY
}

validate_release_dir() {
    local directory="$1"
    python3 - "$directory" <<'PY'
import hashlib
import json
import pathlib
import re
import sys

directory = pathlib.Path(sys.argv[1])
metadata_path = directory / "release.json"
checksum_path = directory / "SHA256SUMS.txt"
try:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
except (OSError, ValueError, TypeError) as error:
    raise SystemExit(f"invalid release metadata: {error}")

version = metadata.get("version")
filename = metadata.get("filename")
fingerprint = metadata.get("source_fingerprint")
expected_sha = metadata.get("sha256")
build = metadata.get("build")
if not isinstance(version, str) or not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)", version):
    raise SystemExit("invalid release version")
if build != int(version.rsplit(".", 1)[1]) + 1:
    raise SystemExit("release build number does not match version")
expected_filename = f"StopWatch-Codex-Agent-{version}.dmg"
if filename != expected_filename:
    raise SystemExit("release filename does not match version")
if not isinstance(fingerprint, str) or not fingerprint:
    raise SystemExit("missing release fingerprint")
if not isinstance(expected_sha, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_sha):
    raise SystemExit("invalid release checksum")
payload = directory / filename
try:
    actual_sha = hashlib.sha256(payload.read_bytes()).hexdigest()
    checksum = checksum_path.read_text(encoding="utf-8")
except OSError as error:
    raise SystemExit(f"missing release file: {error}")
if actual_sha != expected_sha:
    raise SystemExit("release payload checksum mismatch")
if checksum != f"{expected_sha}  {filename}\n":
    raise SystemExit("checksum file does not match metadata")
expected_files = {"release.json", "SHA256SUMS.txt", filename}
actual_files = {path.name for path in directory.iterdir()}
if actual_files != expected_files:
    raise SystemExit("release directory contains stale or unexpected files")
PY
}

publish_latest_release() {
    local staging="$1"
    local latest="$2"
    local parent="$(dirname "$latest")"
    local backup="$parent/.latest-backup.$$"
    if [[ "$(basename "$latest")" != "latest" ]]; then
        echo "Release destination must be named latest" >&2
        return 2
    fi
    validate_release_dir "$staging" || return
    mkdir -p "$parent"
    if [[ -e "$backup" ]]; then
        echo "Release backup already exists: $backup" >&2
        return 2
    fi

    local had_latest=0
    if [[ -e "$latest" ]]; then
        mv "$latest" "$backup"
        had_latest=1
    fi
    if ! mv "$staging" "$latest"; then
        if [[ "$had_latest" == "1" ]]; then
            mv "$backup" "$latest"
        fi
        return 1
    fi
    if ! validate_release_dir "$latest"; then
        rm -rf -- "$latest"
        if [[ "$had_latest" == "1" ]]; then
            mv "$backup" "$latest"
        fi
        return 1
    fi
    if [[ "$had_latest" == "1" ]]; then
        rm -rf -- "$backup"
    fi
}

write_release_metadata() {
    local output="$1"
    local version="$2"
    local fingerprint="$3"
    local filename="$4"
    local sha256="$5"
    local commit="$6"
    local build_number="$7"
    python3 - "$output" "$version" "$fingerprint" "$filename" \
        "$sha256" "$commit" "$build_number" <<'PY'
import datetime
import json
import pathlib
import sys

output, version, fingerprint, filename, sha256, commit, build = sys.argv[1:]
metadata = {
    "version": version,
    "build": int(build),
    "source_fingerprint": fingerprint,
    "filename": filename,
    "sha256": sha256,
    "signing": "adhoc",
    "source_commit": commit,
    "built_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
}
pathlib.Path(output).write_text(
    json.dumps(metadata, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
}

cleanup_release_build() {
    if [[ -n "${RELEASE_BUILD_ROOT:-}" && -d "$RELEASE_BUILD_ROOT" ]]; then
        rm -rf -- "$RELEASE_BUILD_ROOT"
    fi
}

main() {
    local current_version="$(<"$VERSION_FILE")"
    if ! is_release_version "$current_version"; then
        echo "Invalid Agent version: $current_version" >&2
        return 2
    fi

    local release_inputs=()
    local input=""
    while IFS= read -r input; do
        release_inputs+=("$input")
    done < <(release_input_paths)
    local fingerprint="$(
        release_input_fingerprint "$PROJECT_ROOT" "${release_inputs[@]}"
    )"
    local previous_fingerprint="$(
        release_metadata_field "$LATEST_DIR/release.json" source_fingerprint
    )"
    local version="$(
        next_release_version "$current_version" "$previous_fingerprint" "$fingerprint"
    )"
    local patch="${version##*.}"
    local build_number="$((10#$patch + 1))"

    if [[ -n "$previous_fingerprint" && "$previous_fingerprint" == "$fingerprint" ]] &&
        validate_release_dir "$LATEST_DIR"; then
        echo "Latest Agent release is already $version"
        return
    fi

    mkdir -p "$ARTIFACT_ROOT"
    RELEASE_BUILD_ROOT="$(mktemp -d "$ARTIFACT_ROOT/.release-build.XXXXXX")"
    trap cleanup_release_build EXIT HUP INT TERM
    local app_parent="$RELEASE_BUILD_ROOT/app"
    mkdir -p "$app_parent"
    STOPWATCH_APP_PARENT="$app_parent" \
        STOPWATCH_AGENT_VERSION="$version" \
        STOPWATCH_AGENT_BUILD_NUMBER="$build_number" \
        "$INSTALLER"

    local app="$app_parent/StopWatch Codex Agent.app"
    local payload="$RELEASE_BUILD_ROOT/payload"
    local staging="$RELEASE_BUILD_ROOT/latest"
    local filename="StopWatch-Codex-Agent-$version.dmg"
    mkdir -p "$payload" "$staging"
    ditto "$app" "$payload/StopWatch Codex Agent.app"
    ln -s /Applications "$payload/Applications"
    hdiutil create -quiet -volname "StopWatch Codex Agent" \
        -srcfolder "$payload" -ov -format UDZO "$staging/$filename"
    hdiutil verify -quiet "$staging/$filename"

    local sha256="$(shasum -a 256 "$staging/$filename" | awk '{print $1}')"
    printf '%s  %s\n' "$sha256" "$filename" > "$staging/SHA256SUMS.txt"
    local commit="$(git -C "$PROJECT_ROOT" rev-parse HEAD)"
    write_release_metadata "$staging/release.json" "$version" "$fingerprint" \
        "$filename" "$sha256" "$commit" "$build_number"
    validate_release_dir "$staging"
    publish_latest_release "$staging" "$LATEST_DIR"

    local version_temp="$ROOT_DIR/.VERSION.$$"
    printf '%s\n' "$version" > "$version_temp"
    mv "$version_temp" "$VERSION_FILE"
    RELEASE_BUILD_ROOT=""
    rm -rf -- "$(dirname "$staging")"
    trap - EXIT HUP INT TERM
    echo "Built $LATEST_DIR/$filename"
}

if [[ "${STOPWATCH_RELEASE_SOURCE_ONLY:-0}" != "1" ]]; then
    main "$@"
fi
