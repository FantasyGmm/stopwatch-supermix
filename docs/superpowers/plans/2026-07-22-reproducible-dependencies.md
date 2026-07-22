# Reproducible Firmware Dependencies Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a fresh StopWatch checkout reproducibly fetch immutable source dependencies and build the unified firmware without the ignored full XiaoZhi source tree.

**Architecture:** Keep ESP-IDF registry dependencies under `main/idf_component.yml` and `dependencies.lock`, while a strict Python bootstrapper materializes source components from exact Git commits and verifies patched content hashes. Replace the full XiaoZhi snapshot dependency with two tracked, hash-verified build inputs, then enforce both a local dependency preflight and a true clean-checkout build gate.

**Tech Stack:** Python 3 standard library, Git, ESP-IDF 5.5.4 Component Manager, CMake, Bash, Python `unittest`, SHA-256

---

## File Map

- Create `dependency_bootstrap.py`: manifest types, validation, deterministic
  tree hashing, materialization, verification, replacement, and CLI-independent
  dependency logic.
- Modify `fetch_repos.py`: thin command-line entry point for bootstrap,
  `--check`, and `--force`.
- Modify `repos.json`: immutable source revisions and patch hashes.
- Create `mac-codex-agent/tests/test_dependency_bootstrap.py`: unit and local
  Git integration tests for strict dependency behavior.
- Create `tools/xiaozhi/build_default_assets.py`: tracked XiaoZhi v2.2.6 asset
  packer.
- Create `main/assets/fonts/font_puhui_14_1.c`: tracked XiaoZhi Fonts 1.6.0 CJK
  font input.
- Create `third_party_notices/xiaozhi-build-inputs.md`: license and cryptographic
  provenance.
- Delete `patches/xiaozhi-stopwatch-size.patch`: obsolete standalone XiaoZhi
  application patch; it is not part of the unified runtime.
- Modify `main/CMakeLists.txt`: compile the tracked CJK font.
- Modify `main/cmake/unified_assets.cmake`: call the tracked asset packer.
- Modify `scripts/report_firmware_metrics.py`: remove fallback lookup of a
  standalone ignored XiaoZhi image.
- Modify `scripts/build_unified_candidate.sh`: verify source dependencies before
  CMake without downloading them.
- Create `scripts/validate_reproducible_checkout.sh`: clone only tracked files,
  bootstrap dependencies, and run a clean unified build.
- Modify `README.md`: document bootstrap, ownership, upgrades, and clean-checkout
  verification.
- Modify `mac-codex-agent/tests/test_firmware_font_contract.py`: use and verify
  the tracked font.
- Create `mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py`:
  source contracts for provenance, CMake paths, build preflight, and removal of
  supported full-tree references.

### Task 1: Add Strict Manifest And Content Hashing

**Files:**
- Create: `dependency_bootstrap.py`
- Create: `mac-codex-agent/tests/test_dependency_bootstrap.py`

- [ ] **Step 1: Write failing manifest and hash tests**

Create tests that import the root module by adding `ROOT` to `sys.path`, then
exercise strict parsing and deterministic tree hashing:

```python
class DependencyManifestTests(unittest.TestCase):
    def test_parses_exact_revision_and_patch_hash(self):
        specs = parse_manifest([
            {
                "url": "https://github.com/m5stack/M5IOE1.git",
                "path": "components/M5IOE1",
                "revision": "37db04861687858b6748f5dbd1a84439a6635c46",
                "release": "1.0.8",
                "with_submodules": False,
                "patch": {
                    "path": "patches/M5IOE1.patch",
                    "sha256": "5b2bd8fd54742c9f9968124d188c32dac98f8a15916af08b31f829d6b3f84778",
                },
            }
        ], ROOT)
        self.assertEqual(specs[0].revision, "37db04861687858b6748f5dbd1a84439a6635c46")

    def test_rejects_branch_unknown_key_and_path_escape(self):
        for mutation in (
            {"branch": "main"},
            {"unexpected": True},
            {"path": "../outside"},
        ):
            entry = valid_entry()
            entry.update(mutation)
            with self.subTest(mutation=mutation), self.assertRaises(DependencyError):
                parse_manifest([entry], ROOT)

    def test_content_hash_ignores_git_metadata_and_stamp(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            (root / "source.cpp").write_text("one\n", encoding="utf-8")
            initial = content_tree_hash(root)
            (root / ".git").mkdir()
            (root / ".git" / "HEAD").write_text("ignored\n", encoding="utf-8")
            write_stamp(root, {"content_sha256": initial})
            self.assertEqual(content_tree_hash(root), initial)
            (root / "source.cpp").write_text("two\n", encoding="utf-8")
            self.assertNotEqual(content_tree_hash(root), initial)
```

- [ ] **Step 2: Run the tests and verify they fail**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_dependency_bootstrap.py -v
```

Expected: import failure for `dependency_bootstrap`.

- [ ] **Step 3: Implement manifest types and deterministic hashing**

Define these public interfaces in `dependency_bootstrap.py`:

```python
STAMP_NAME = ".stopwatch-dependency.json"

class DependencyError(RuntimeError):
    pass

@dataclasses.dataclass(frozen=True)
class PatchSpec:
    path: pathlib.Path
    sha256: str

@dataclasses.dataclass(frozen=True)
class RepoSpec:
    url: str
    path: pathlib.Path
    revision: str
    release: str | None
    with_submodules: bool
    patch: PatchSpec | None

def parse_manifest(raw_entries, root, allowed_schemes=("https",)) -> list[RepoSpec]:
    """Validate every entry before returning any RepoSpec."""

def load_manifest(root: pathlib.Path) -> list[RepoSpec]:
    raw = json.loads((root / "repos.json").read_text(encoding="utf-8"))
    return parse_manifest(raw, root)

def content_tree_hash(root: pathlib.Path) -> str:
    """Hash sorted relative POSIX paths, file modes, symlink targets, and bytes."""

def write_stamp(root: pathlib.Path, payload: dict) -> None:
    (root / STAMP_NAME).write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
```

Validation must require exact key sets, HTTPS URLs, unique paths under the root
`components/` directory, 40 lowercase hexadecimal revisions, 64 lowercase
hexadecimal patch hashes, existing patch files, booleans for
`with_submodules`, and no symlink/path escape from the project root.

The tree hash must ignore every path component named `.git` and the root stamp,
while including relative path, executable bit, symlink target, and file bytes in
sorted order.

- [ ] **Step 4: Run focused tests**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_dependency_bootstrap.py -v
```

Expected: all manifest and tree-hash tests pass.

- [ ] **Step 5: Commit the pure dependency model**

```bash
git add dependency_bootstrap.py mac-codex-agent/tests/test_dependency_bootstrap.py
git commit -m "Add strict dependency manifest model"
```

### Task 2: Implement Fail-Closed Bootstrap And Verification

**Files:**
- Modify: `dependency_bootstrap.py`
- Modify: `fetch_repos.py`
- Modify: `mac-codex-agent/tests/test_dependency_bootstrap.py`

- [ ] **Step 1: Add local Git integration tests**

Create a temporary upstream repository with one committed file. Parse a fixture
manifest with `allowed_schemes=("file",)` and assert:

```python
def test_materialize_and_check_exact_revision(self):
    upstream, revision = create_local_upstream(self.temp_path)
    spec = local_spec(upstream, revision)
    materialize_all([spec], self.project_root)
    self.assertEqual(verify_all([spec], self.project_root), [])

def test_wrong_patch_hash_fails_without_replacing_destination(self):
    destination = self.project_root / "components" / "fixture"
    destination.mkdir(parents=True)
    (destination / "keep.txt").write_text("keep\n", encoding="utf-8")
    spec = dataclasses.replace(
        local_spec(self.upstream, self.revision),
        patch=PatchSpec(self.patch_path, "0" * 64),
    )
    with self.assertRaisesRegex(DependencyError, "patch hash"):
        materialize_all([spec], self.project_root, force=True)
    self.assertEqual((destination / "keep.txt").read_text(), "keep\n")

def test_expected_patch_is_clean_but_extra_edit_is_preserved(self):
    materialize_all([self.patched_spec], self.project_root)
    destination = self.project_root / "components" / "fixture"
    self.assertEqual(verify_all([self.patched_spec], self.project_root), [])
    (destination / "extra.txt").write_text("user change\n", encoding="utf-8")
    with self.assertRaisesRegex(DependencyError, "local changes"):
        materialize_all([self.patched_spec], self.project_root)
    self.assertTrue((destination / "extra.txt").exists())
```

Also test missing destination, wrong `HEAD`, missing stamp, changed content,
failed `git apply --check`, submodule revision capture, `--check` exit status,
and that staging one failing dependency leaves all previous destinations intact.

- [ ] **Step 2: Run integration tests and verify failure**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_dependency_bootstrap.py -v
```

Expected: failures for missing `materialize_all`, `verify_all`, and CLI behavior.

- [ ] **Step 3: Implement staging, patching, stamps, and replacement**

Add these interfaces:

```python
@dataclasses.dataclass(frozen=True)
class VerificationIssue:
    path: pathlib.Path
    category: str
    message: str

def run_git(*args: str, cwd: pathlib.Path | None = None) -> str:
    completed = subprocess.run(
        ["git", *args], cwd=cwd, text=True, capture_output=True
    )
    if completed.returncode:
        raise DependencyError(completed.stderr.strip() or completed.stdout.strip())
    return completed.stdout.strip()

def stage_component(spec: RepoSpec, stage_root: pathlib.Path) -> pathlib.Path:
    """Clone, detach at revision, initialize submodules, apply verified patch, stamp."""

def verify_component(spec: RepoSpec) -> VerificationIssue | None:
    """Check URL, HEAD, submodule revisions, patch hash, stamp, and content hash."""

def verify_all(specs: list[RepoSpec], root: pathlib.Path) -> list[VerificationIssue]:
    return [issue for spec in specs if (issue := verify_component(spec))]

def materialize_all(specs: list[RepoSpec], root: pathlib.Path, force=False) -> None:
    """Stage all missing/replaced specs, then replace destinations with rollback."""
```

Use `tempfile.TemporaryDirectory(dir=root)` so rename operations stay on one
filesystem. Before staging, reject mismatched existing destinations unless
`force=True`. After all staging succeeds, rename existing destinations to
temporary backups, move staged directories into place, and restore all backups
if any replacement raises. Remove backups only after all replacements succeed.

The stamp JSON must contain `schema`, `url`, `revision`, `release`,
`with_submodules`, sorted `submodules`, `patch_sha256`, and `content_sha256`.

- [ ] **Step 4: Replace `fetch_repos.py` with the thin CLI**

Keep the public command stable:

```python
def parse_args(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--force", action="store_true")
    return parser.parse_args(argv)

def main(argv=None):
    root = pathlib.Path(__file__).resolve().parent
    args = parse_args(argv)
    try:
        specs = load_manifest(root)
        if args.check:
            issues = verify_all(specs, root)
            for issue in issues:
                print(f"{issue.category}: {issue.path}: {issue.message}", file=sys.stderr)
            return 1 if issues else 0
        materialize_all(specs, root, force=args.force)
        return 0
    except DependencyError as error:
        print(f"dependency error: {error}", file=sys.stderr)
        return 2
```

Reject `--check --force` as an invalid combination and use `raise
SystemExit(main())` at the entry point.

- [ ] **Step 5: Run focused tests and syntax checks**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_dependency_bootstrap.py -v
python3 -m py_compile dependency_bootstrap.py fetch_repos.py
```

Expected: all tests pass and both modules compile without output.

- [ ] **Step 6: Commit strict bootstrap behavior**

```bash
git add dependency_bootstrap.py fetch_repos.py mac-codex-agent/tests/test_dependency_bootstrap.py
git commit -m "Make dependency bootstrap fail closed"
```

### Task 3: Pin Every Source Component

**Files:**
- Modify: `repos.json`
- Modify: `mac-codex-agent/tests/test_dependency_bootstrap.py`

- [ ] **Step 1: Add a production-manifest test**

```python
def test_production_manifest_uses_verified_revisions(self):
    expected = {
        "components/mooncake": "572a7e488641f1c3f22e85d65c27f095ecf27d12",
        "components/mooncake_log": "554d55cd8c6e551b911b99cad274a46929f05bae",
        "components/smooth_ui_toolkit": "2c8b6c572192047636c78ea3ae95dc5be4955fce",
        "components/M5GFX": "53a7184601f3667b030ba141c58b87ce2acfaa2a",
        "components/ArduinoJson": "77771d3c07668e01d8f52acb03910c1110bb373f",
        "components/lvgl": "85aa60d18b3d5e5588d7b247abf90198f07c8a63",
        "components/M5IOE1": "37db04861687858b6748f5dbd1a84439a6635c46",
        "components/M5PM1": "8f1f1a60b3040088cd0ca2b9eb9d024ccc4d907f",
        "components/BMI270_BMM150_Sensor": "ae8bc480bca638d3458e65d5e9a4fff4270c1430",
    }
    actual = {spec.path.relative_to(ROOT).as_posix(): spec.revision for spec in load_manifest(ROOT)}
    self.assertEqual(actual, expected)
```

- [ ] **Step 2: Run the production-manifest test and verify failure**

Run:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_dependency_bootstrap.DependencyManifestTests.test_production_manifest_uses_verified_revisions -v
```

Expected: failure because the current manifest has `branch` fields.

- [ ] **Step 3: Convert `repos.json` to immutable entries**

Use the exact revisions from the test. Preserve current release values as
`release`. Add these patch objects:

```json
"patch": {
  "path": "patches/M5IOE1.patch",
  "sha256": "5b2bd8fd54742c9f9968124d188c32dac98f8a15916af08b31f829d6b3f84778"
}
```

```json
"patch": {
  "path": "patches/M5PM1.patch",
  "sha256": "7b4bffc382a49ecdd8520b67c15ebfa4f83572ced94faa27f4df2e799d7bc9a5"
}
```

Set `with_submodules` explicitly on every entry; only
`BMI270_BMM150_Sensor` is `true`.

- [ ] **Step 4: Bootstrap into an empty temporary project fixture**

Run the production manifest test, then run the real bootstrap only after moving
any existing local component trees out of the verification path or by using a
clean checkout in Task 6. At this task, verify non-network behavior against the
current local tree:

```bash
python3 -m unittest mac-codex-agent/tests/test_dependency_bootstrap.py -v
python3 fetch_repos.py --check
```

Expected: tests pass; `--check` may report missing stamps for pre-migration local
components, which is the expected fail-closed result and must not modify them.

- [ ] **Step 5: Commit the immutable manifest**

```bash
git add repos.json mac-codex-agent/tests/test_dependency_bootstrap.py
git commit -m "Pin source component revisions"
```

### Task 4: Replace The Full XiaoZhi Tree With Minimal Inputs

**Files:**
- Create: `tools/xiaozhi/build_default_assets.py`
- Create: `main/assets/fonts/font_puhui_14_1.c`
- Create: `third_party_notices/xiaozhi-build-inputs.md`
- Delete: `patches/xiaozhi-stopwatch-size.patch`
- Modify: `main/CMakeLists.txt`
- Modify: `main/cmake/unified_assets.cmake`
- Modify: `mac-codex-agent/tests/test_firmware_font_contract.py`
- Create: `mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py`

- [ ] **Step 1: Write failing XiaoZhi provenance and path contracts**

```python
ASSET_SCRIPT_SHA256 = "b0fd24a9f2be39e94ecfaf25cac8a5f53f77a5dcc0d9ea58167c193357905338"
FONT_SHA256 = "0684c8dbe08fe5ab374037b222fcc1376c28290146828baa67cf353ac465eac1"

def sha256(relative_path):
    return hashlib.sha256((ROOT / relative_path).read_bytes()).hexdigest()

class ReproducibleDependencyFirmwareContractTests(unittest.TestCase):
    def test_tracked_xiaozhi_inputs_match_verified_sources(self):
        self.assertEqual(sha256("tools/xiaozhi/build_default_assets.py"), ASSET_SCRIPT_SHA256)
        self.assertEqual(sha256("main/assets/fonts/font_puhui_14_1.c"), FONT_SHA256)

    def test_supported_build_does_not_reference_full_xiaozhi_tree(self):
        paths = [
            "main/CMakeLists.txt",
            "main/cmake/unified_assets.cmake",
            "scripts/build_unified_candidate.sh",
            "scripts/report_firmware_metrics.py",
            "README.md",
        ]
        for path in paths:
            self.assertNotIn("third_party/xiaozhi-esp32", read(path), path)
```

Update the font contract to read
`ROOT / "main/assets/fonts/font_puhui_14_1.c"`.

- [ ] **Step 2: Run contracts and verify missing-input failures**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py \
  mac-codex-agent/tests/test_firmware_font_contract.py -v
```

Expected: missing tracked asset script and font failures.

- [ ] **Step 3: Copy the two verified inputs without editing their bytes**

Use byte-preserving copies from the current ignored snapshot:

```bash
mkdir -p tools/xiaozhi main/assets/fonts third_party_notices
cp third_party/xiaozhi-esp32/scripts/build_default_assets.py \
  tools/xiaozhi/build_default_assets.py
cp third_party/xiaozhi-esp32/managed_components/78__xiaozhi-fonts/src/font_puhui_14_1.c \
  main/assets/fonts/font_puhui_14_1.c
shasum -a 256 tools/xiaozhi/build_default_assets.py \
  main/assets/fonts/font_puhui_14_1.c
```

Expected hashes are exactly the constants in Step 1. Do not format or normalize
either upstream file.

- [ ] **Step 4: Add the provenance notice**

Document the XiaoZhi v2.2.6 repository/tag/commit/path/hash and the
`78/xiaozhi-fonts` 1.6.0 registry component hash/path/file hash. State that both
sources are MIT licensed, that local copies are unmodified, and that the notice
must be updated with every input change.

- [ ] **Step 5: Redirect CMake to tracked inputs**

In `main/CMakeLists.txt` set:

```cmake
set(CODEX_CJK_FONT
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/font_puhui_14_1.c"
)
```

In `main/cmake/unified_assets.cmake` set:

```cmake
set(UNIFIED_ASSETS_SCRIPT
    "${PROJECT_DIR}/tools/xiaozhi/build_default_assets.py"
)
```

Delete `patches/xiaozhi-stopwatch-size.patch`; its standalone application and
sdkconfig changes are outside the unified runtime.

- [ ] **Step 6: Run hash, font, and source contracts**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py \
  mac-codex-agent/tests/test_firmware_font_contract.py \
  mac-codex-agent/tests/test_xiaozhi_probe_firmware_contract.py -v
```

Expected: all tests pass.

- [ ] **Step 7: Commit minimal XiaoZhi inputs**

```bash
git add tools/xiaozhi/build_default_assets.py \
  main/assets/fonts/font_puhui_14_1.c \
  third_party_notices/xiaozhi-build-inputs.md \
  main/CMakeLists.txt main/cmake/unified_assets.cmake \
  mac-codex-agent/tests/test_firmware_font_contract.py \
  mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py \
  patches/xiaozhi-stopwatch-size.patch
git commit -m "Track minimal XiaoZhi build inputs"
```

### Task 5: Enforce Build Preflight And Document Upgrades

**Files:**
- Modify: `scripts/build_unified_candidate.sh`
- Modify: `scripts/report_firmware_metrics.py`
- Modify: `README.md`
- Modify: `mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py`
- Modify: `mac-codex-agent/tests/test_report_firmware_metrics.py`

- [ ] **Step 1: Add failing preflight and metric contracts**

Assert the build script contains this preflight before its first `idf.py` call:

```bash
if ! python3 "$ROOT/fetch_repos.py" --check; then
    echo "Source dependencies are missing or invalid." >&2
    echo "Run: python3 ./fetch_repos.py" >&2
    exit 2
fi
```

Add a metrics test that calls `parse_args` without `--assistant-bin` and confirms
launcher-baseline collection no longer probes an ignored XiaoZhi image. Extend
the source contract to reject `third_party/xiaozhi-esp32` from the metric script
and README.

- [ ] **Step 2: Run tests and verify failures**

Run:

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py \
  mac-codex-agent/tests/test_report_firmware_metrics.py -v
```

Expected: preflight and legacy metric fallback assertions fail.

- [ ] **Step 3: Add the source dependency preflight**

Insert the exact preflight from Step 1 after environment variables are defined
and before optional `fullclean`. It must use the system `python3`, perform no
download, and preserve build directories on failure.

- [ ] **Step 4: Remove the standalone Assistant metric fallback**

Delete the `args.source == "launcher-baseline"` block that constructs
`third_party/xiaozhi-esp32/build-m5stack-stopwatch/xiaozhi.bin`. Keep explicit
`--assistant-bin` support for reading deliberately supplied historical images.

- [ ] **Step 5: Rewrite dependency and upgrade documentation**

README must state:

- `python3 ./fetch_repos.py` is the only source-component bootstrap command;
- `python3 ./fetch_repos.py --check` is offline and non-mutating;
- source trees and ESP-IDF managed components are disposable generated inputs;
- only the XiaoZhi asset packer and CJK font are tracked locally;
- Assistant runtime upgrades modify `main/idf_component.yml` and
  `dependencies.lock`;
- XiaoZhi build-input upgrades update the two files and provenance hashes;
- complete `third_party/xiaozhi-esp32/` is neither required nor supported by the
  unified build.

- [ ] **Step 6: Run focused tests and Bash syntax check**

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py \
  mac-codex-agent/tests/test_report_firmware_metrics.py -v
bash -n scripts/build_unified_candidate.sh
```

Expected: all tests pass and Bash syntax check produces no output.

- [ ] **Step 7: Commit build preflight and documentation**

Because these files contain unrelated working-tree edits, stage only this
task's hunks and inspect the cached diff:

```bash
git add -p scripts/build_unified_candidate.sh scripts/report_firmware_metrics.py \
  README.md mac-codex-agent/tests/test_report_firmware_metrics.py
git add mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py
git diff --cached --check
git diff --cached
git commit -m "Enforce reproducible build dependencies"
```

### Task 6: Add And Pass The True Clean-Checkout Gate

**Files:**
- Create: `scripts/validate_reproducible_checkout.sh`
- Modify: `mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py`
- Modify: `docs/superpowers/specs/2026-07-22-reproducible-dependencies-design.md`

- [ ] **Step 1: Add a failing script contract**

Assert the validation script:

- creates a temporary directory;
- uses `git clone --no-local` from the repository root;
- rejects pre-existing `components`, `managed_components`, and `third_party` in
  the clone;
- runs `python3 ./fetch_repos.py`;
- runs `./scripts/build_unified_candidate.sh --clean`;
- records the source commit and firmware SHA256;
- never symlinks dependency directories from the developer checkout.

- [ ] **Step 2: Run the contract and verify missing-script failure**

```bash
python3 -m unittest \
  mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py -v
```

Expected: missing `scripts/validate_reproducible_checkout.sh` failure.

- [ ] **Step 3: Implement the clean-checkout script**

Use this flow:

```bash
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/stopwatch-clean-checkout.XXXXXX")"
trap 'rm -rf "$WORK_DIR"' EXIT
CLONE="$WORK_DIR/StopWatch"

git clone --no-local "$ROOT" "$CLONE"
for generated in components managed_components third_party build build-unified; do
    if [[ -e "$CLONE/$generated" ]]; then
        echo "Clean clone unexpectedly contains $generated" >&2
        exit 2
    fi
done

cd "$CLONE"
python3 ./fetch_repos.py
./scripts/build_unified_candidate.sh --clean
git rev-parse HEAD
shasum -a 256 build-unified/StopWatch-UserDemo.bin build-unified/generated_assets.bin
```

Require `idf.py` in the environment or source `${IDF_PATH:-$HOME/esp/esp-idf-v5.5.4}/export.sh`
using the same explicit error behavior as `run_unified_offline_validation.sh`.

- [ ] **Step 4: Commit the gate before running it**

The local clone must include the validation script and all implementation
commits, so commit first:

```bash
git add scripts/validate_reproducible_checkout.sh \
  mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py
git commit -m "Add clean checkout firmware gate"
```

- [ ] **Step 5: Run all automated tests before the expensive gate**

```bash
python3 -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
python3 -m unittest mac-codex-agent/tests/test_dependency_bootstrap.py -v
git diff --check
```

Expected: all dependency tests pass. If an existing unrelated firmware contract
still fails, record it separately and prove the dependency suite is green before
continuing.

- [ ] **Step 6: Run the true clean-checkout build**

```bash
./scripts/validate_reproducible_checkout.sh
```

Expected:

- all nine source repositories materialize at their locked revisions;
- both M5 patches apply and stamps verify;
- ESP-IDF restores managed components from `dependencies.lock`;
- unified firmware and generated assets build successfully;
- capacity gates pass;
- the script prints commit and SHA256 values;
- no ignored directory from the developer checkout is reused.

- [ ] **Step 7: Record verified evidence in the design status**

Change the design status to `Implemented and clean-checkout verified`. Add a
short verification section containing the commit, app size, app headroom,
assets size, firmware SHA256, assets SHA256, and validation date. Do not mark
hardware behavior verified because this dependency migration does not flash.

- [ ] **Step 8: Commit verification evidence**

```bash
git add docs/superpowers/specs/2026-07-22-reproducible-dependencies-design.md
git commit -m "Record reproducible build verification"
```

### Task 7: Final Regression And Commit-Scope Audit

**Files:**
- Verify only; modify only a focused test or implementation file if a regression
  is attributable to this feature.

- [ ] **Step 1: Verify supported references and tracked inputs**

```bash
rg -n "third_party/xiaozhi-esp32" \
  main scripts README.md fetch_repos.py dependency_bootstrap.py \
  mac-codex-agent/tests
git check-ignore -v components third_party/xiaozhi-esp32 managed_components
git ls-files tools/xiaozhi/build_default_assets.py \
  main/assets/fonts/font_puhui_14_1.c \
  third_party/xiaozhi-esp32 components
```

Expected: no supported full-tree references; generated directories are ignored;
only the two minimal XiaoZhi inputs are tracked.

- [ ] **Step 2: Verify provenance hashes**

```bash
shasum -a 256 tools/xiaozhi/build_default_assets.py \
  main/assets/fonts/font_puhui_14_1.c
```

Expected:

```text
b0fd24a9f2be39e94ecfaf25cac8a5f53f77a5dcc0d9ea58167c193357905338  tools/xiaozhi/build_default_assets.py
0684c8dbe08fe5ab374037b222fcc1376c28290146828baa67cf353ac465eac1  main/assets/fonts/font_puhui_14_1.c
```

- [ ] **Step 3: Audit feature commits and unrelated WIP**

```bash
git log --oneline --decorate -8
git status --short
git diff --cached --name-only
git diff --check
```

Expected: no staged files; all pre-existing unrelated working-tree modifications
remain present and are absent from the dependency-migration commits.

- [ ] **Step 4: Do not flash automatically**

Report the clean-build artifact and validation evidence. If hardware validation
is requested, first ask the user to enter download mode; after flashing, tell
the user to exit download mode before functional checks.
