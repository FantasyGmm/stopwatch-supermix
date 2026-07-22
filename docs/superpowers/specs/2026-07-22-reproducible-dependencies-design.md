# Reproducible Firmware Dependencies Design

Date: 2026-07-22
Status: Implemented and clean-checkout verified

## Purpose

Make a fresh StopWatch checkout buildable without committing generated dependency
trees or the complete XiaoZhi source tree. A developer with ESP-IDF 5.5.4 and
network access must be able to fetch immutable dependencies, build the unified
firmware, and obtain the same dependency graph and XiaoZhi build inputs used by
the project.

The design preserves the existing product boundary:

- `components/`, `managed_components/`, and `third_party/xiaozhi-esp32/` remain
  generated or external directories and stay ignored by Git;
- the in-process Assistant runtime continues to come from the pinned Brookesia
  and XiaoZhi Agent components in `main/idf_component.yml` and
  `dependencies.lock`;
- only the two XiaoZhi files directly consumed by the unified StopWatch build
  are kept as reviewed project inputs;
- dependency upgrades are explicit commits, never floating downloads during a
  normal build.

## Current Problem

The checkout currently has three dependency mechanisms with different levels of
reproducibility:

1. ESP-IDF managed components are declared in `main/idf_component.yml` and
   resolved in `dependencies.lock`. This path is already versioned and
   hash-checked by the component manager.
2. Local components are listed in `repos.json` and fetched by `fetch_repos.py`,
   but entries use branch or tag names. The fetcher does not verify the resolved
   commit and silently skips a patch that cannot be applied.
3. The build directly reads files from the ignored
   `third_party/xiaozhi-esp32/` snapshot. A fresh checkout does not contain that
   directory, and no tracked manifest recreates it.

The local ignored trees are approximately 340 MB for `components/` and 1 GB for
`third_party/xiaozhi-esp32/`. Tracking either complete tree would make repository
history unnecessarily large and would obscure which upstream inputs the product
actually uses.

## Dependency Ownership Model

Each dependency belongs to exactly one mechanism.

### ESP-IDF Managed Components

Brookesia, XiaoZhi Agent, ESP-SR, ESP WiFi Connect, codecs, and other registry
components remain declared in `main/idf_component.yml`. The tracked
`dependencies.lock` is the authoritative resolution, including versions and
component hashes. `managed_components/` remains ignored and disposable.

### Source Components

Mooncake, Mooncake Log, Smooth UI Toolkit, M5GFX, ArduinoJson, LVGL, M5IOE1,
M5PM1, and BMI270/BMM150 remain external checkouts under `components/` because
the project applies local patches or consumes their source-tree layout directly.

Their authoritative resolution moves from mutable `branch` fields in
`repos.json` to exact 40-character Git commit IDs. Human-readable release names
may be retained as metadata, but the fetcher must only check out the commit ID.

### Minimal XiaoZhi Build Inputs

The unified firmware does not compile the standalone XiaoZhi application. It
currently consumes only:

- `scripts/build_default_assets.py` from `78/xiaozhi-esp32`;
- `src/font_puhui_14_1.c` from `78/xiaozhi-fonts`.

These files move into tracked, purpose-specific project paths:

```text
tools/xiaozhi/build_default_assets.py
main/assets/fonts/font_puhui_14_1.c
third_party_notices/xiaozhi-build-inputs.md
```

This adds about 1.8 MB instead of retaining a 1 GB source snapshot. The notice
records license, source repository, source path, version, immutable revision or
registry component hash, and file SHA256.

The verified asset-script provenance is:

- repository: `https://github.com/78/xiaozhi-esp32.git`;
- tag: `v2.2.6`;
- commit: `49ac8a6da399f27a9546d4f73640b7f86c24bac6`;
- file SHA256:
  `b0fd24a9f2be39e94ecfaf25cac8a5f53f77a5dcc0d9ea58167c193357905338`.

The current font provenance is:

- registry component: `78/xiaozhi-fonts` version `1.6.0`;
- component hash:
  `19ee829a7439702e18084a313b7b86017d3b8d17a094684575edc76bb6d11023`;
- file SHA256:
  `0684c8dbe08fe5ab374037b222fcc1376c28290146828baa67cf353ac465eac1`.

## Locked Source Manifest

`repos.json` remains the source-component manifest but adopts a strict schema:

```json
{
  "url": "https://github.com/m5stack/M5IOE1.git",
  "path": "components/M5IOE1",
  "revision": "37db04861687858b6748f5dbd1a84439a6635c46",
  "release": "1.0.8",
  "with_submodules": false,
  "patch": {
    "path": "patches/M5IOE1.patch",
    "sha256": "5b2bd8fd54742c9f9968124d188c32dac98f8a15916af08b31f829d6b3f84778"
  }
}
```

`revision` and patch SHA256 are mandatory where applicable. `release` is
informational and cannot affect resolution. Manifest validation rejects unknown
keys, duplicate paths, paths outside `components/`, non-HTTPS repository URLs,
non-commit revisions, and malformed hashes.

## Fetch Behavior

`fetch_repos.py` becomes a fail-closed dependency bootstrapper.

The bootstrapper first stages every requested component without replacing any
existing destination. For each staged component it must:

1. validate the complete manifest before changing any directory;
2. fetch the exact commit into a temporary sibling directory;
3. initialize pinned submodules recursively when requested;
4. verify `HEAD` equals the requested commit;
5. verify the patch file hash;
6. require `git apply --check` to succeed and then apply the patch;
7. calculate a deterministic content-tree hash over dependency files, excluding
   Git metadata and the generated stamp;
8. write a generated dependency stamp containing URL, revision, submodule
   revisions, patch hash, and the content-tree hash.

Only after every component has staged and validated successfully may the
bootstrapper replace destination directories, one directory at a time. A
network, revision, submodule, or patch failure therefore leaves every previous
generated dependency untouched.

Applied project patches intentionally change a checkout relative to upstream,
so ordinary Git dirty status is not a sufficient safety check. The default
operation compares the current content-tree hash with its generated stamp and
never overwrites a component containing additional local changes. It reports
the path and exits nonzero. An explicit `--force` option may replace generated
dependency trees, but it does not modify tracked project files.

`--check` performs no downloads and verifies every required component against
the manifest and generated stamp. Missing, dirty, wrong-revision, or
wrong-patch dependencies produce distinct error messages and nonzero status.

Patch application is mandatory. The current behavior that logs an incompatible
patch and continues is removed because it can produce a successful but
behaviorally different firmware.

## Build Flow

The supported fresh-checkout flow is:

```bash
source /path/to/esp-idf-v5.5.4/export.sh
python3 ./fetch_repos.py
./scripts/build_unified_candidate.sh --clean
```

`build_unified_candidate.sh` runs `fetch_repos.py --check` before CMake. It does
not silently fetch source components. If they are absent, it fails with the
bootstrap command. On a machine without populated `managed_components/`,
ESP-IDF may then download registry components during reconfiguration, using the
tracked manifest and lockfile as its exact resolution.

`main/CMakeLists.txt` reads the tracked font source, and
`main/cmake/unified_assets.cmake` reads the tracked asset builder. No supported
build, metric, test, or documentation path may require
`third_party/xiaozhi-esp32/` after migration.

## Upgrade Flow

Dependency upgrades are reviewable changes with separate ownership.

### Source Component Upgrade

1. Resolve the intended upstream release to an immutable commit.
2. Update `repos.json` revision and release metadata.
3. Refresh and hash any project patch.
4. Bootstrap into an empty dependency tree.
5. Run host tests, source contracts, clean firmware build, capacity gates, and
   relevant hardware regression.
6. Commit manifest, patch, and documentation changes together.

### Assistant Runtime Upgrade

1. Update the required Brookesia or XiaoZhi Agent versions in
   `main/idf_component.yml`.
2. Regenerate and commit `dependencies.lock`.
3. Confirm the dependency graph does not mix incompatible Brookesia minor
   versions.
4. Run Assistant startup, conversation, stop, resource-release, and repeated
   lifecycle validation on hardware.

### XiaoZhi Build-Input Upgrade

1. Fetch the selected XiaoZhi tag or registry component into a temporary
   directory.
2. Copy only the two approved inputs into their tracked project paths.
3. Update the notice with source revision, component hash, and file hashes.
4. Verify the generated assets and firmware build.
5. Treat any additional required XiaoZhi file as a design change; do not expand
   the vendored surface implicitly.

## Failure Handling

- Network failure leaves the previous generated dependencies untouched.
- A missing upstream commit, moved tag, or wrong checkout fails before build.
- Patch drift fails bootstrap and identifies the component and patch.
- A managed-component lock mismatch fails ESP-IDF reconfiguration.
- A tracked XiaoZhi input hash mismatch fails a source-contract test.
- Any residual reference to `third_party/xiaozhi-esp32/` fails migration tests.
- A dirty generated component is preserved unless the developer explicitly
  requests replacement.

## Tests And Verification

Automated coverage must include:

- manifest schema validation;
- exact revision checkout;
- patch hash and patch-application failure;
- dirty-tree protection;
- `--check` success and each failure category;
- tracked XiaoZhi input provenance hashes;
- absence of supported `third_party/xiaozhi-esp32/` references;
- build-script dependency preflight;
- an actual clean-checkout bootstrap and `--clean` unified firmware build;
- existing host, Python contract, size, and dependency-version gates.

The clean-checkout gate must start without `components/`, `managed_components/`,
`third_party/`, build directories, or a generated sdkconfig. Reusing the
developer's local dependency directories by symlink does not satisfy this gate.

Hardware flashing is not part of dependency bootstrap. A firmware produced by
the new clean-checkout path is flashed only after the user explicitly enters
download mode. Functional acceptance then covers Launcher startup, Assistant
conversation, wake word, audio, WiFi, return to Launcher, and repeated resource
release.

## Migration Sequence

1. Add tests for strict manifest and current fail-open fetch behavior.
2. Convert every `repos.json` entry to an exact upstream commit and patch hash.
3. Implement strict bootstrap and local verification.
4. Add the two tracked XiaoZhi inputs and provenance notice.
5. Redirect CMake and metric/test references to the tracked inputs.
6. Remove every supported build dependency on the ignored XiaoZhi snapshot.
7. Run the clean-checkout gate and compare firmware size and generated assets
   against the current validated build.
8. Update README dependency and upgrade instructions.

The ignored local `third_party/xiaozhi-esp32/` directory may remain on a
developer machine for reference, but it has no role in supported builds and is
safe to delete after migration verification.

## Implementation Verification

Verified on 2026-07-22 from tracked source commit
`46509d1c48cd4e607020bb761cc3e3a8662f4a65` with ESP-IDF 5.5.4. The
`scripts/validate_reproducible_checkout.sh` gate cloned only tracked project
files into a temporary directory, confirmed that generated dependency and build
directories were absent, restored all locked source and managed components, and
completed a clean unified firmware build.

The resulting capacity and provenance evidence was:

- application image: 6,022,432 bytes;
- application partition: 7,864,320 bytes;
- application headroom: 1,841,888 bytes (23.42%);
- generated assets: 291,198 bytes;
- application SHA256:
  `3da6b25a1b1df7ebb668af1cc58f2d8c4cbf61b4864aa70109924e2f2faa1f11`;
- generated-assets SHA256:
  `93b87d059d1c2a31f1dd65bbdfa4d5b7275774f6f12e82eb84a1737acad3723f`;
- capacity gate: passed.

This verification did not flash hardware. Hardware acceptance remains a
separate release gate and requires the user to place the watch in download mode
before any write.

## Acceptance Criteria

- A fresh checkout builds after one explicit dependency-bootstrap command.
- Every source component resolves to an immutable commit.
- Patch incompatibility is a hard failure.
- `managed_components/`, `components/`, and the complete XiaoZhi tree remain
  untracked.
- The supported build has no dependency on `third_party/xiaozhi-esp32/`.
- Only the reviewed XiaoZhi asset builder and required CJK font source are
  tracked, with license and cryptographic provenance.
- Assistant runtime upgrades remain controlled by the component manifest and
  lockfile rather than by replacing an ignored source snapshot.
- Clean-checkout firmware passes existing build, capacity, and runtime
  validation gates before hardware release.
