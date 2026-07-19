# USB Debug App Launch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a USB debug command that opens an installed Mooncake watch application without human interaction.

**Architecture:** A host-testable fixed-buffer parser recognizes `debug app list`, `debug app open <name>`, and the unattended-test companion `debug app home`. An ESP-only adapter reads USB Serial/JTAG without creating a task, then delegates to a Launcher-owned same-thread target so the existing Launcher close/open lifecycle remains authoritative. A dependency-free Python command discovers the watch port, sends the command, validates the acknowledgement, and can keep streaming logs.

**Tech Stack:** C++17, Mooncake, ESP-IDF 5.5.4 USB Serial/JTAG driver, Python 3 standard library, `unittest`, native host tests.

---

## File map

- `main/debug/debug_command_parser.h/.cpp`: allocation-free command grammar and error classification.
- `main/debug/debug_app_control.h/.cpp`: one registered Launcher target and stable `OK`/`BUSY`/`NOT_FOUND` results.
- `main/debug/debug_command_service.h/.cpp`: fixed line buffer, ESP USB reads, response logging, compile-time no-op implementation.
- `main/CMakeLists.txt`: include the focused `debug/` sources in the main component.
- `main/apps/app_launcher/app_launcher.h/.cpp`: lifetime registration, name-to-ID resolution while Launcher is open, and normal child close for Home.
- `main/main.cpp`: one non-blocking debug update per existing main-loop iteration.
- `main/Kconfig.projbuild`, `sdkconfig.unified.defaults`: debug-only build gate.
- `tests/debug_command_test.cpp`: parser and target-registry host tests.
- `scripts/run_unified_offline_validation.sh`: execute the new host test.
- `scripts/watch_debug.py`: port discovery, serial transport, acknowledgement matching, and follow mode.
- `mac-codex-agent/tests/test_watch_debug.py`: host-command behavior.
- `mac-codex-agent/tests/test_debug_command_firmware_contract.py`: compile gate and no-task source contract.

### Task 1: Lock the command grammar

**Files:**
- Create: `main/debug/debug_command_parser.h`
- Create: `main/debug/debug_command_parser.cpp`
- Create: `tests/debug_command_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write the failing parser test**

Create cases that require exact prefix matching, case-insensitive application names, missing-name rejection, and overlong-line classification:

```cpp
assert(ParseDebugCommand("debug app list").kind == DebugCommandKind::AppList);
const auto open = ParseDebugCommand("debug app open AsSiStAnT");
assert(open.kind == DebugCommandKind::AppOpen);
assert(open.argument == "assistant");
assert(ParseDebugCommand("app open assistant").error == DebugCommandError::InvalidCommand);
assert(ParseDebugCommand("debug app open").error == DebugCommandError::InvalidCommand);
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
c++ -std=c++17 -Wall -Wextra -Werror -Imain \
  tests/debug_command_test.cpp main/debug/debug_command_parser.cpp \
  -o /tmp/stopwatch-debug-command-test
```

Expected: compilation fails because the parser files and symbols do not exist.

- [ ] **Step 3: Implement the minimal parser**

Define stable types and a 64-byte protocol limit:

```cpp
enum class DebugCommandKind : uint8_t { None, AppList, AppOpen };
enum class DebugCommandError : uint8_t { None, InvalidCommand, LineTooLong };

struct DebugCommand {
    DebugCommandKind kind = DebugCommandKind::None;
    DebugCommandError error = DebugCommandError::None;
    std::array<char, 49> argument{};
};

constexpr std::size_t debug_command_max_line_length = 64;
DebugCommand ParseDebugCommand(std::string_view line);
bool DebugAppNameEquals(std::string_view installed_name, std::string_view normalized_name);
```

Trim only trailing carriage return, require the exact `debug ` prefix, lowercase the application name into the fixed array, and reject empty, whitespace-containing, or oversized names.

- [ ] **Step 4: Run the parser test and verify GREEN**

Run the command from Step 2 followed by `/tmp/stopwatch-debug-command-test`.

Expected: exit code 0 with no output.

- [ ] **Step 5: Add the host test to offline validation**

Compile it into `$HOST_TEST_DIR/debug_command_test`, run it after the shared Wi-Fi test, and update subsequent step numbers while keeping the final whitespace gate.

### Task 2: Preserve Launcher ownership of application lifecycle

**Files:**
- Create: `main/debug/debug_app_control.h`
- Create: `main/debug/debug_app_control.cpp`
- Modify: `tests/debug_command_test.cpp`
- Modify: `main/apps/app_launcher/app_launcher.h`
- Modify: `main/apps/app_launcher/app_launcher.cpp`

- [ ] **Step 1: Add failing target-registry tests**

Use a fake target and prove no target returns `BUSY`, registered targets receive normalized names, unknown applications propagate `NOT_FOUND`, and unregistering restores `BUSY`:

```cpp
FakeDebugAppTarget target;
assert(RequestDebugAppOpen("assistant") == DebugAppOpenResult::Busy);
RegisterDebugAppTarget(&target);
target.next_result = DebugAppOpenResult::Ok;
assert(RequestDebugAppOpen("assistant") == DebugAppOpenResult::Ok);
assert(target.last_name == "assistant");
UnregisterDebugAppTarget(&target);
assert(RequestDebugAppOpen("assistant") == DebugAppOpenResult::Busy);
```

- [ ] **Step 2: Run the host test and verify RED**

Add `main/debug/debug_app_control.cpp` to the Task 1 compile command.

Expected: compilation fails because the target interface and registry are missing.

- [ ] **Step 3: Implement the target interface and registry**

Use one raw pointer because all registration and dispatch happen on the Mooncake main thread:

```cpp
enum class DebugAppOpenResult : uint8_t { Ok, Busy, NotFound };

class DebugAppTarget {
public:
    virtual ~DebugAppTarget() = default;
    virtual DebugAppOpenResult openDebugApp(std::string_view name) = 0;
    virtual void logDebugAppList() const = 0;
};

void RegisterDebugAppTarget(DebugAppTarget* target);
void UnregisterDebugAppTarget(DebugAppTarget* target);
DebugAppOpenResult RequestDebugAppOpen(std::string_view name);
bool RequestDebugAppList();
```

Registration replaces no different live target. Unregistration clears only the same pointer.

- [ ] **Step 4: Integrate the Launcher**

Make `AppLauncher` implement `DebugAppTarget`. Register at the end of `onLauncherOpen()` and unregister at the start of `onLauncherClose()`. Resolve names from `getAppProps()` using lowercase comparison and call the inherited `openApp(props.appID)` only after a match:

```cpp
for (const auto& props : getAppProps()) {
    if (DebugAppNameEquals(props.info.name, name)) {
        return openApp(props.appID) ? DebugAppOpenResult::Ok
                                    : DebugAppOpenResult::Busy;
    }
}
return DebugAppOpenResult::NotFound;
```

`logDebugAppList()` emits one `DEBUG APP LIST ...` line without retaining app IDs.

- [ ] **Step 5: Run the host test and existing Launcher contracts**

Run:

```bash
/tmp/stopwatch-debug-command-test
python3 -m unittest \
  mac-codex-agent/tests/test_clock_firmware_contract.py \
  mac-codex-agent/tests/test_unified_runtime_firmware_contract.py
```

Expected: all tests pass.

### Task 3: Add the task-free USB firmware adapter

**Files:**
- Create: `main/debug/debug_command_service.h`
- Create: `main/debug/debug_command_service.cpp`
- Create: `mac-codex-agent/tests/test_debug_command_firmware_contract.py`
- Modify: `main/CMakeLists.txt`
- Modify: `main/main.cpp`
- Modify: `main/Kconfig.projbuild`
- Modify: `sdkconfig.unified.defaults`

- [ ] **Step 1: Write the failing firmware contract**

Assert that the configuration gate exists and is enabled only in the unified debug defaults, USB Serial/JTAG is the primary console, `main/CMakeLists.txt` includes `debug/*.cpp`, the main loop calls `GetDebugCommandService().update()`, and the implementation performs one bounded read from the USB Serial/JTAG hardware RX FIFO. It contains none of `usb_serial_jtag_driver_install`, a standard-input read, `xTaskCreate`, `std::thread`, or a polling `while (1)`.

- [ ] **Step 2: Run the contract and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_debug_command_firmware_contract.py
```

Expected: failure because the configuration and service do not exist.

- [ ] **Step 3: Add the build gate**

Add to `main/Kconfig.projbuild`:

```kconfig
config STOPWATCH_DEBUG_COMMANDS
    bool "Enable physical USB debug commands"
    default n
    help
        Accept fixed, local-only debug commands from USB Serial/JTAG.
```

Add `CONFIG_STOPWATCH_DEBUG_COMMANDS=y` only to `sdkconfig.unified.defaults`.

- [ ] **Step 4: Implement the non-blocking service**

Add `"debug/*.cpp"` to `MY_SRCS`. The enabled implementation owns `std::array<char, 65> _line`, a current length, and an overflow flag. The unified debug defaults select USB Serial/JTAG as the primary ESP-IDF console, because ESP-IDF's secondary console is output-only. Runtime testing also showed that the shared console standard-input path does not deliver host bytes in this firmware, so each `update()` performs one bounded `usb_serial_jtag_ll_read_rxfifo()` call into a small stack buffer. This is the same no-driver hardware primitive ESP-IDF's own USB VFS uses; it installs no driver, creates no ring buffer, and never waits. Parsed complete lines call `RequestDebugAppList()` or `RequestDebugAppOpen()` and log exactly one stable response. The disabled implementation is an inline no-op selected with `#if CONFIG_STOPWATCH_DEBUG_COMMANDS`.

- [ ] **Step 5: Wire the existing main loop**

Call the debug update between watchdog feeding and `GetMooncake().update()`:

```cpp
GetHAL().feedTheDog();
GetDebugCommandService().update();
GetMooncake().update();
```

- [ ] **Step 6: Run the contract and offline tests**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_debug_command_firmware_contract.py
./scripts/run_unified_offline_validation.sh
```

Expected: the focused contract and complete offline gate pass.

### Task 4: Add the dependency-free host command

**Files:**
- Create: `scripts/watch_debug.py`
- Create: `mac-codex-agent/tests/test_watch_debug.py`

- [ ] **Step 1: Write failing host-command tests**

Test pure helpers and an injected fake transport:

```python
self.assertEqual(discover_port(["/dev/cu.usbmodem1101"]), "/dev/cu.usbmodem1101")
with self.assertRaises(PortDiscoveryError):
    discover_port([])
self.assertEqual(build_command("open", "Assistant"), b"debug app open assistant\n")
self.assertTrue(response_matches("open", "assistant", "DEBUG APP OPEN assistant OK"))
```

Also require a non-zero result for `BUSY`, `NOT_FOUND`, timeout, and multiple auto-discovered ports.

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_watch_debug.py
```

Expected: import failure because `scripts/watch_debug.py` does not exist.

- [ ] **Step 3: Implement port discovery and serial transport**

Use only `argparse`, `glob`, `os`, `select`, `termios`, and `time`. Configure the file descriptor to raw 115200 baud, send the request once the port is open, assemble newline-delimited responses, and close the descriptor in `finally`. `--follow` prints all subsequent bytes after a matching success and remains active until interrupted.

- [ ] **Step 4: Implement the CLI**

Support:

```text
watch_debug.py [--port PATH] [--timeout SECONDS] [--follow] app list
watch_debug.py [--port PATH] [--timeout SECONDS] [--follow] app open NAME
watch_debug.py [--port PATH] [--timeout SECONDS] [--follow] app home
```

Normalize the name before sending. Exit 0 only for the matching list or `OK`; exit 2 for local argument/port errors and exit 3 for firmware rejection or timeout.

- [ ] **Step 5: Run the host tests and syntax check**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_watch_debug.py
python3 -m py_compile scripts/watch_debug.py
```

Expected: all tests pass and compilation produces no output.

### Task 5: Build, flash, and automate the Assistant smoke test

**Files:**
- Modify: `docs/superpowers/reports/brookesia-xiaozhi-compatibility.md`
- Modify: `docs/superpowers/reports/unified-runtime-validation.md`

- [ ] **Step 1: Run final static gates**

Run:

```bash
./scripts/run_unified_offline_validation.sh
git diff --check
```

Expected: all Python/native tests, clean unified build, capacity gates, and whitespace check pass.

- [ ] **Step 2: Flash with the verified backup guard**

Use the existing unified candidate flash helper with `/dev/cu.usbmodem1101` and the verified backup `backups/watch-full-flash-20260718-173922.bin`. Write bootloader, partition table, and factory app only; do not write NVS or assets unless their generated hashes changed and the helper explicitly requires them.

- [ ] **Step 3: Launch Assistant without touching the watch**

Run:

```bash
python3 scripts/watch_debug.py --port /dev/cu.usbmodem1101 --timeout 20 --follow app open assistant
```

Expected: `DEBUG APP OPEN assistant OK`, Launcher close, Assistant open, Wi-Fi IP, successful Xiaozhi activation, and Running with no reset.

- [ ] **Step 4: Validate lifecycle and error responses**

Run `app home`, verify the existing app close and Launcher reopen lifecycle, then run the open command a second time and verify stable stopped-boundary diagnostics. While Assistant is open, send another open request and verify `BUSY`; send an unknown name from Launcher and verify `NOT_FOUND`. Neither response may reset or change the running application.

- [ ] **Step 5: Update hardware reports**

Record firmware SHA-256, binary size, reset reason, before/connected/service/running/stopped memory snapshots, debug command acknowledgements, activation outcome, repeat-open outcome, and any remaining manual-only checks. Mark the Launcher/Assistant merged startup gate passed only if Assistant reaches Running and exits without restart.
