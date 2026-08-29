# Agent Menu and Latest Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the approved dual-source Agent menu and one portable latest-only macOS Agent release with automatic patch versioning.

**Architecture:** Keep presentation ownership in `app_state.py`, build three source-specific submenus in `menu_app.py`, and retain all existing controller actions. Extend the existing self-contained app builder so bundle paths are relative, then wrap it with a release script that fingerprints production inputs, calculates the next version, builds a DMG in a temporary directory, and atomically publishes only `artifacts/macos-agent/latest`.

**Tech Stack:** Python 3, PyObjC/AppKit, Bash, macOS `codesign`, `hdiutil`, `shasum`, and `unittest`.

**Spec:** `docs/superpowers/specs/2026-08-29-agent-menu-and-latest-release-design.md`

## Global Constraints

- Codex and Trae are parallel sources; no global task menu item is allowed.
- Codex owns quota and Codex alert counts/details; Trae owns its project and current task details.
- The only artifact destination is `artifacts/macos-agent/latest/`; successful publication replaces it atomically.
- The first release is `1.0.0`; changed successful releases increment only the patch; unchanged rebuilds and failed builds do not increment.
- The app remains universal2, self-contained, key-free, and relocatable.
- Preserve unrelated `.superpowers/` files and `mac-codex-agent/tests/test_codex_task_focus_view_contract.py` changes.

---

### Task 1: Encode dual-source menu presentation

**Files:**
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/menu_app.py`
- Test: `mac-codex-agent/tests/test_app_state.py`
- Test: `mac-codex-agent/tests/test_menu_app.py`

**Interfaces:**
- Consumes: `AgentSnapshot`, `CodexSnapshot`, `TraeSnapshot`, and existing action selectors.
- Produces: source-specific summary strings plus `codex_submenu`, `trae_submenu`, and `device_submenu` menu trees.

- [ ] **Step 1: Add failing presentation tests**

Add assertions that Codex summary text includes Codex model and task counts,
Trae summary text includes Trae project/state, and neither presentation exposes
an unqualified global task summary.

- [ ] **Step 2: Run presentation tests and verify the new assertions fail**

Run: `python3 -m unittest mac-codex-agent/tests/test_app_state.py -v`

Expected: FAIL because the source-specific summary fields do not exist.

- [ ] **Step 3: Add minimal source-specific presentation fields**

Extend `MenuPresentation` and `present()` with explicit Agent health, Codex
summary, Trae summary, and device summary strings. Keep the existing detailed
fields so submenu rows use the established copy and state mapping.

- [ ] **Step 4: Add failing AppKit menu structure tests**

Assert that the root menu has `Codex`, `Trae`, and `Device & Voice` parent
items; that each has its own submenu; that Codex alerts exist only inside the
Codex submenu; that Trae task text exists only inside the Trae submenu; and
that all existing repair/actions remain reachable.

- [ ] **Step 5: Run menu tests and verify the structure assertions fail**

Run: `python3 -m unittest mac-codex-agent/tests/test_menu_app.py -v`

Expected: FAIL because the current menu places Codex alert tasks at root and
renders Trae details as a separate root section.

- [ ] **Step 6: Build the approved menu hierarchy**

Create compact source parent items with their corresponding submenus, move the
existing detailed labels into the correct submenu, add the health/version
header, and update refresh logic without changing controller actions or repair
visibility rules.

- [ ] **Step 7: Run focused menu and presentation tests**

Run: `python3 -m unittest mac-codex-agent/tests/test_app_state.py mac-codex-agent/tests/test_menu_app.py -v`

Expected: PASS.

### Task 2: Make the app bundle relocatable and versioned

**Files:**
- Create: `mac-codex-agent/VERSION`
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Test: `mac-codex-agent/tests/test_install_script.py`

**Interfaces:**
- Consumes: `STOPWATCH_AGENT_VERSION` and optional `STOPWATCH_APP_PARENT` environment variables.
- Produces: a self-contained app whose Info.plist version matches the requested release and contains no absolute Python paths.

- [ ] **Step 1: Add failing installer contract tests**

Assert that `VERSION` is `1.0.0`, bundle version values come from
`STOPWATCH_AGENT_VERSION`, `LSEnvironment` contains no absolute `PYTHONHOME` or
`PYTHONPATH`, and the runtime launcher resolves `Contents` relative to its own
executable path.

- [ ] **Step 2: Run the installer contract tests and verify failure**

Run: `python3 -m unittest mac-codex-agent/tests/test_install_script.py -v`

Expected: FAIL on the hard-coded `2.0`/`2` version and path-bound Info.plist.

- [ ] **Step 3: Implement relocatable launch and injected versions**

Read and validate a strict `MAJOR.MINOR.PATCH` version, derive the numeric build
number, generate a small universal launcher that finds its own bundle before
setting Python environment variables, and keep the existing embedded Python
executable as a separate runtime binary. Allow an isolated app-parent override
for release builds while preserving the default `~/Applications` install.

- [ ] **Step 4: Run installer tests**

Run: `python3 -m unittest mac-codex-agent/tests/test_install_script.py -v`

Expected: PASS, including existing atomic publication and bundle integrity
contracts.

### Task 3: Publish the latest-only release DMG

**Files:**
- Create: `mac-codex-agent/build_release.sh`
- Create: `mac-codex-agent/tests/test_release_build.py`
- Create: `artifacts/macos-agent/README.md`
- Modify: `.gitignore`
- Modify: `mac-codex-agent/README.md`

**Interfaces:**
- Consumes: tracked production inputs, `mac-codex-agent/VERSION`, and `install_launch_agent.sh`.
- Produces: `artifacts/macos-agent/latest/StopWatch-Codex-Agent-<version>.dmg`, `SHA256SUMS.txt`, and `release.json`.

- [ ] **Step 1: Add failing release-policy tests**

Use temporary directories and source-only shell functions to verify first
release `1.0.0`, unchanged idempotence, changed-input patch increment, failed
publication preservation, version agreement, and removal of the preceding DMG.

- [ ] **Step 2: Run release tests and verify failure**

Run: `python3 -m unittest mac-codex-agent/tests/test_release_build.py -v`

Expected: FAIL because the release builder does not exist.

- [ ] **Step 3: Implement fingerprinting, version calculation, and atomic publication**

Add strict shell functions that hash production inputs, compare the current
`release.json`, calculate the candidate patch version, build into a temporary
app parent, create/verify the DMG, write checksum and JSON metadata, then swap
the complete `latest` directory only after all checks pass. Update `VERSION`
only after successful publication.

- [ ] **Step 4: Ignore generated latest contents but keep its contract documented**

Track `artifacts/macos-agent/README.md`; ignore
`artifacts/macos-agent/latest/`. Document the one-command release build,
latest-only behavior, version rules, and the current ad-hoc/Gatekeeper caveat.

- [ ] **Step 5: Run release and installer tests**

Run: `python3 -m unittest mac-codex-agent/tests/test_release_build.py mac-codex-agent/tests/test_install_script.py -v`

Expected: PASS.

### Task 4: Build and validate the actual release

**Files:**
- Generated: `artifacts/macos-agent/latest/*`
- Modify after successful build: `mac-codex-agent/VERSION`

**Interfaces:**
- Consumes: all completed implementation tasks.
- Produces: the actual latest DMG for installation on another Mac.

- [ ] **Step 1: Run focused Agent regressions**

Run: `python3 -m unittest mac-codex-agent/tests/test_app_state.py mac-codex-agent/tests/test_menu_app.py mac-codex-agent/tests/test_install_script.py mac-codex-agent/tests/test_release_build.py -v`

Expected: PASS.

- [ ] **Step 2: Build the release**

Run: `mac-codex-agent/build_release.sh`

Expected: one DMG plus matching checksum and metadata under
`artifacts/macos-agent/latest/`.

- [ ] **Step 3: Verify bundle and artifact evidence**

Mount the DMG read-only, verify `codesign --verify --deep --strict`, confirm
both `arm64` and `x86_64` for every Mach-O candidate, compare Info.plist and
metadata versions, verify the checksum, and confirm no absolute source or home
path appears in Info.plist.

- [ ] **Step 4: Run repository hygiene checks**

Run: `git diff --check` and `git status --short`.

Expected: no whitespace errors; only task files plus the preserved unrelated
changes appear.
