# Formal Release Convergence

## Current state

The currently deployed watch firmware is a release candidate rather than a
traditional Debug build:

- release/size compiler optimization;
- INFO maximum logging;
- assertions enabled;
- USB development commands enabled through
  `CONFIG_STOPWATCH_DEBUG_COMMANDS=y`;
- temporary development diagnostics may still be present.

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
