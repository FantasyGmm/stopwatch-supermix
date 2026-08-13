# CODEC UAC Kconfig Compatibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make clean unified firmware builds resolve optional USB UAC dependencies without enabling USB audio.

**Architecture:** Add the missing compatibility symbol to the tracked project Kconfig with a disabled default. Lock that invariant with a source-level firmware contract, then prove it through the repository's clean-checkout build gate.

**Tech Stack:** ESP-IDF 5.5.4 Kconfig, Python `unittest`, shell-based unified validation.

---

### Task 1: Lock the tracked compatibility contract

**Files:**
- Modify: `mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py`

- [ ] **Step 1: Write the failing test**

Add a test that reads `main/Kconfig.projbuild`, requires
`config CODEC_UAC_SUPPORT` with `default n`, and rejects
`CONFIG_CODEC_UAC_SUPPORT=y` from `sdkconfig.defaults` and
`sdkconfig.unified.defaults`.

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_reproducible_dependencies_firmware_contract.py -v
```

Expected: the new test fails because the tracked Kconfig does not define the
symbol.

### Task 2: Add the minimal tracked definition

**Files:**
- Modify: `main/Kconfig.projbuild`

- [ ] **Step 1: Implement the compatibility symbol**

Add:

```kconfig
config CODEC_UAC_SUPPORT
    bool "Enable optional USB Audio Class host support"
    default n
    help
        Keep the upstream optional dependency condition defined while USB
        Audio Class hosting remains disabled on StopWatch hardware.
```

- [ ] **Step 2: Run focused tests**

Run the Task 1 command again. Expected: all tests pass.

- [ ] **Step 3: Commit the red-green change**

Commit the Kconfig definition and regression test together.

### Task 3: Prove reproducibility and integration

**Files:**
- No source changes expected.

- [ ] **Step 1: Run clean-checkout build**

```bash
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/validate_reproducible_checkout.sh
```

Expected: dependency bootstrap, clean firmware build, artifact hashes, and the
reproducibility gate all succeed.

- [ ] **Step 2: Run unified host validation**

```bash
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: 57/57 stages pass.

- [ ] **Step 3: Check the patch**

```bash
git diff --check
```

Expected: no output.
