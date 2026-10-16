#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "$SCRIPT_DIR/common.sh"

usage() {
    echo "Usage: collect_licenses.sh CARGO_LOCK APP_RESOURCES_DIR" >&2
}

if [[ $# != 2 ]]; then usage; exit 2; fi
LOCK_FILE="$1"
OUTPUT_RESOURCES="$2"
[[ -f "$LOCK_FILE" ]] || { echo "Cargo.lock is missing: $LOCK_FILE" >&2; exit 1; }

CARGO_HOME="${CARGO_HOME:-$FUSION_ROOT/cargo-home}"
REGISTRY_SOURCE_ROOT="$CARGO_HOME/registry/src"
[[ -d "$REGISTRY_SOURCE_ROOT" ]] || { echo "Cargo registry sources are missing: $REGISTRY_SOURCE_ROOT" >&2; exit 1; }

WORK_ROOT="$(mktemp -d "$FUSION_ROOT/.license-collect.XXXXXX")"
cleanup() { rm -rf -- "$WORK_ROOT"; }
trap cleanup EXIT

PACKAGES_FILE="$WORK_ROOT/packages.tsv"
awk '
  /^\[\[package\]\]$/ {
    if (name != "" && source ~ /registry\+/) print name "\t" version
    name = ""; version = ""; source = ""; next
  }
  /^name = / { name = $3; gsub(/"/, "", name) }
  /^version = / { version = $3; gsub(/"/, "", version) }
  /^source = / { source = $3; gsub(/"/, "", source) }
  END { if (name != "" && source ~ /registry\+/) print name "\t" version }
' "$LOCK_FILE" > "$PACKAGES_FILE"
[[ -s "$PACKAGES_FILE" ]] || { echo "Cargo.lock contains no registry packages" >&2; exit 1; }

manifest_field() {
    local manifest="$1" key="$2"
    awk -v key="$key" '
      /^\[package\][[:space:]]*$/ { in_package = 1; next }
      /^\[/ { if (in_package) exit }
      in_package && $0 ~ "^[[:space:]]*" key "[[:space:]]*=" {
        value = $0
        sub(/^[^=]*=[[:space:]]*/, "", value)
        sub(/[[:space:]]+#.*$/, "", value)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
        if (value ~ /^".*"$/) { sub(/^"/, "", value); sub(/"$/, "", value) }
        print value
        exit
      }
    ' "$manifest"
}

contains_mit() {
    [[ "$1" =~ (^|[^[:alnum:]-])MIT([^[:alnum:]-]|$) ]]
}

MIT_LICENSE_SOURCE=""
while IFS= read -r candidate; do
    candidate_manifest="$(dirname -- "$candidate")/Cargo.toml"
    candidate_license="$(manifest_field "$candidate_manifest" license)"
    if contains_mit "$candidate_license"; then
        MIT_LICENSE_SOURCE="$candidate"
        break
    fi
done < <(find "$REGISTRY_SOURCE_ROOT" -mindepth 3 -maxdepth 3 -type f \
    \( -iname 'license-mit' -o -iname 'license-mit.txt' -o -iname 'license-mit.md' \) \
    -print | LC_ALL=C sort)
if [[ -z "$MIT_LICENSE_SOURCE" ]]; then
    echo "No local MIT license text is available for registry packages without a license file" >&2
    exit 1
fi

STAGED_LICENSES="$WORK_ROOT/third-party-licenses"
NOTICE_FILE="$WORK_ROOT/THIRD-PARTY-NOTICES.txt"
mkdir -p "$STAGED_LICENSES"
{
    cat <<'EOF'
Third-party Rust dependency licenses

This inventory is generated from mac-agent-rust/Cargo.lock and each locked crate's
Cargo.toml license metadata and locally cached license, copying, or notice files.
The inventory includes registry dependencies for every target in the lockfile.

EOF
    printf 'Cargo.lock SHA-256: %s\n\n' "$(shasum -a 256 "$LOCK_FILE" | awk '{print $1}')"
} > "$NOTICE_FILE"

package_count=0
while IFS=$'\t' read -r name version; do
    [[ "$name" =~ ^[A-Za-z0-9_-]+$ && "$version" =~ ^[A-Za-z0-9.+_-]+$ ]] || {
        echo "Unsafe package name or version in Cargo.lock: $name $version" >&2
        exit 1
    }
    source_dir="$(find "$REGISTRY_SOURCE_ROOT" -mindepth 2 -maxdepth 2 -type d \
        -name "$name-$version" -print -quit)"
    [[ -n "$source_dir" && -f "$source_dir/Cargo.toml" ]] || {
        echo "Locked crate source is missing: $name $version" >&2
        exit 1
    }

    expression="$(manifest_field "$source_dir/Cargo.toml" license)"
    declared_file="$(manifest_field "$source_dir/Cargo.toml" license-file)"
    package_key="$name-$version"
    package_output="$STAGED_LICENSES/$package_key"
    mkdir -p "$package_output"
    file_list="$WORK_ROOT/files-$package_count.txt"
    find "$source_dir" -maxdepth 1 -type f \
        \( -iname 'license*' -o -iname 'copying*' -o -iname 'notice*' \) \
        ! -iname '*.rs' -print | LC_ALL=C sort > "$file_list"
    copied_text=0
    while IFS= read -r license_path; do
        [[ -n "$license_path" ]] || continue
        cp "$license_path" "$package_output/$(basename -- "$license_path")"
        copied_text=1
    done < "$file_list"

    if [[ -n "$declared_file" ]]; then
        case "$declared_file" in
            /*|..|../*|*/../*|*/..) echo "Unsafe license-file path in $source_dir/Cargo.toml" >&2; exit 1 ;;
        esac
        license_path="$source_dir/$declared_file"
        [[ -f "$license_path" && ! -L "$license_path" ]] || {
            echo "Declared license file is missing: $name $version ($declared_file)" >&2
            exit 1
        }
        cp "$license_path" "$package_output/DECLARED-LICENSE-$(basename -- "$declared_file")"
        copied_text=1
    fi

    if [[ "$copied_text" == "0" ]]; then
        if contains_mit "$expression"; then
            cp "$MIT_LICENSE_SOURCE" "$package_output/LICENSE-MIT.txt"
            copied_text=1
        else
            echo "No local license text and no MIT option for $name $version ($expression)" >&2
            exit 1
        fi
    fi
    [[ "$copied_text" == "1" ]] || { echo "No license text copied for $name $version" >&2; exit 1; }
    if [[ -z "$expression" ]]; then expression="(see included license file)"; fi
    {
        printf '%s %s\n' "$name" "$version"
        printf '  Declared license: %s\n' "$expression"
        printf '  Text: third-party-licenses/%s/\n\n' "$package_key"
    } >> "$NOTICE_FILE"
    package_count=$((package_count + 1))
done < "$PACKAGES_FILE"

[[ "$package_count" -gt 0 ]] || { echo "No third-party licenses were collected" >&2; exit 1; }
mkdir -p "$OUTPUT_RESOURCES"
rm -rf -- "$OUTPUT_RESOURCES/third-party-licenses"
mkdir -p "$OUTPUT_RESOURCES/third-party-licenses"
cp -R "$STAGED_LICENSES/." "$OUTPUT_RESOURCES/third-party-licenses/"
cp "$NOTICE_FILE" "$OUTPUT_RESOURCES/THIRD-PARTY-NOTICES.txt"
echo "Collected license texts for $package_count locked registry packages"
