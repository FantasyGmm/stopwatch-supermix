# Formal Release Convergence

## Current source state

The unified firmware defaults now describe the formal build:

- release/size compiler optimization;
- INFO maximum logging;
- assertions enabled;
- USB Serial/JTAG output retained for field evidence;
- USB development commands, Assistant lifecycle trace, and battery calibration
  runtime disabled through `CONFIG_STOPWATCH_DEBUG_COMMANDS=n`;
- Assistant heap/task/resource success snapshots compiled only for an explicit
  development build;
- low-frequency warning/error and BLE/security/storage/reset evidence retained.

The state of an installed watch is a separate gate. A clean configure, build,
flash, boot, and physical smoke check are required before calling the device a
formal build.

## Trigger

When the user asks to “简化成正式版” or otherwise requests the formal
production build, use the scope below without requiring the user to redefine
it.

## Convergence scope

1. Disable USB development commands and ensure no debug command parser or
   control surface remains reachable in the production image.
2. Remove temporary UI labels, diagnostic markers, one-off instrumentation,
   performance timestamps, counters, and high-frequency lifecycle logs added
   only for development or fault isolation.
3. Reduce or remove nonessential INFO logs. Keep only low-frequency,
   privacy-safe evidence needed to diagnose boot/reset/crash, authentication,
   BLE connection, storage migration, and unrecoverable voice or transport
   failures.
4. Retain firmware, protocol, and build identifiers; security validation;
   recovery state; NVS schema and migration markers; critical error codes; and
   anything required for compatibility, safe failure, or field diagnosis.
5. Before deleting a label, command, or log, search firmware, Mac Agent, test,
   script, validation, and compatibility consumers. Update or remove only the
   development-specific consumers.

## Release gate

Build from the production configuration, then run the complete host and
contract suite, clean release build and capacity gates, package and hash
checks, boot validation, and physical smoke acceptance before flashing.

At handoff, report exactly which development diagnostics were removed and
which minimal field diagnostics remain.
