# USB Debug App Launch Design

## Goal

Allow automated firmware debugging to open any installed watch application over the connected USB debug port. A human must not need to tap the Launcher after flashing or rebooting the watch.

## Scope

The first version supports three commands:

- `app list`
- `app open <name>`
- `app home`

Application names are matched case-insensitively against Mooncake's installed application metadata. The implementation must not hard-code Mooncake application IDs. `app home` closes the Launcher's currently tracked child through Mooncake's normal close lifecycle so unattended tests can repeat. Simulating arbitrary buttons, BLE transport, Wi-Fi transport, and production remote control are outside this version.

## Architecture

### Firmware command input

A small `DebugCommandService` reads bytes non-blockingly from the ESP32-S3 USB Serial/JTAG interface during the existing main loop. It creates no FreeRTOS task and performs no polling outside the normal Mooncake update cadence.

The parser uses a fixed-size ASCII line buffer. Newline submits a command; overflow or malformed input resets the buffer and returns a deterministic error. The service is compiled only when `CONFIG_STOPWATCH_DEBUG_COMMANDS=y`. The unified hardware-debug configuration enables it, while a production configuration can compile it out completely.

### Launcher integration

`AppLauncher` registers one same-thread debug handler for its lifetime. `app open <name>` is accepted only while Launcher has no running child, resolves the requested name from `getAppProps()`, and calls the existing `AppLauncherBase::openApp(appID)` path. This preserves the established sequence:

1. Launcher schedules the target application.
2. Launcher closes and releases its UI.
3. Mooncake opens the target application.

The command never calls `Mooncake::openApp()` behind Launcher's back. If Launcher is not currently open, the command returns `BUSY`. `app home` calls `Mooncake::closeApp()` only for `AppLauncherBase::getRunningAppId()`, after which the existing sleeping-state logic reopens Launcher. An unknown application returns `NOT_FOUND`. A successfully scheduled application returns `OK` and logs the resolved name and ID.

### Host command

`scripts/watch_debug.py` provides the user-facing command:

```text
python3 scripts/watch_debug.py app open assistant
python3 scripts/watch_debug.py app list
python3 scripts/watch_debug.py app home
```

The script auto-discovers one `/dev/cu.usbmodem*` port, accepts `--port` when discovery is ambiguous, uses 115200 baud, sends one newline-terminated command, and waits for a matching firmware acknowledgement. A follow mode keeps the same serial handle open and streams subsequent firmware logs so automated tests can launch an app and observe its complete lifecycle without competing with a second serial monitor.

## Protocol

Requests are plain ASCII lines prefixed with `debug ` to avoid interpreting arbitrary terminal input:

```text
debug app list
debug app open assistant
debug app home
```

Responses use a stable machine-readable prefix:

```text
DEBUG APP LIST codex,assistant,clock,badge,imu,fft,setup
DEBUG APP OPEN assistant OK
DEBUG APP OPEN unknown NOT_FOUND
DEBUG APP OPEN assistant BUSY
DEBUG APP HOME OK
DEBUG APP HOME BUSY
DEBUG ERROR INVALID_COMMAND
DEBUG ERROR LINE_TOO_LONG
```

The host command exits successfully only after the matching `OK` or list response. `BUSY`, `NOT_FOUND`, timeout, no port, and ambiguous ports are non-zero exits with concise diagnostics.

## Resource and safety boundaries

- No background task, queue, BLE service, Wi-Fi socket, heap-sized input buffer, or persistent setting is added.
- Input is accepted only from the physical USB debug interface.
- The maximum command line is 64 bytes, including arguments.
- The service owns no application lifecycle. It delegates open and Home requests exclusively to Launcher.
- Disabling `CONFIG_STOPWATCH_DEBUG_COMMANDS` removes the parser, USB reads, and Launcher debug handler from the build.
- Existing button, touch, Home, app-close, and Launcher behavior remain unchanged.

## Verification

Automated tests cover command parsing, normalization, line overflow, unknown commands, Launcher availability, name-to-ID resolution, port discovery, response matching, timeout, and non-zero error exits. A firmware contract test verifies the compile-time gate and rejects a dedicated debug task.

Hardware validation performs the following without touching the watch:

1. Flash and reboot the unified candidate.
2. Run `watch_debug.py app open assistant --follow`.
3. Confirm the acknowledgement, Launcher close, Assistant open, Wi-Fi connection, activation, running state, and absence of a reset.
4. Run `watch_debug.py app home`, confirm normal close and Launcher reopen, then repeat the command.
5. Verify `NOT_FOUND` and `BUSY` do not disturb the running application.
