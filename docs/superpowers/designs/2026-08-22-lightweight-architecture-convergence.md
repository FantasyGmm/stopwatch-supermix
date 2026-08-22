# Lightweight Architecture Convergence Design

**Date:** 2026-08-22
**Status:** Approved

## Goal

Reduce current maintenance and lifecycle risk without a broad rewrite. Work in
this order: simplify stale test/document/manifest coupling, make task route
ownership match one BLE session, then centralize only the firmware resources
whose cleanup currently appears in several lifecycle branches.

## Scope

### 1. Tests, documents, and manifests

- Remove duplicate Python module entries from the Agent installer and its test
  fixture.
- Stop treating dated hardware reports as live behavior contracts. Current
  tests must exercise current scripts and configuration instead of grepping
  historical evidence prose.
- Reconcile current README wording with the established policy: ordinary
  firmware flashing neither creates nor requires a backup; recovery tools
  remain available for explicit recovery or one-time layout migration.
- Keep completed design and hardware evidence in Git. This pass does not bulk
  delete historical documents.

### 2. Route registry ownership

One connected BLE session owns one logical `TaskRouteRegistry`. The same
registry instance is used by status/detail publication and task voice
submission. The runtime resets it at the start of each serialized connection,
so a collision remains fail-closed for the affected session but does not poison
later sessions. Reusing and resetting the object preserves existing dependency
injection and avoids rebuilding the submitter stack.

The registry remains collision-safe and keeps the existing 64-bit opaque route
format. No route wire format, task submission API, or at-most-once behavior
changes.

### 3. Firmware runtime session

Add a small `CodexRuntimeSession` facade around the BLE transport, input action
controller, production voice controller, diagnostic voice probe, and
pinned-microphone controller. It centralizes the repeated disconnect and
shutdown cleanup for those resources. The extraction should reuse current
constructors and call order; it must not introduce a second lifecycle model.

`AppCodexStatus` continues to own LVGL views, gestures, task presentation,
secure-pairing policy, Agent registry persistence, Wi-Fi fallback, and status
decoding. The facade exposes narrow accessors so call sites can migrate without
redesigning those behaviors.

## Failure and lifecycle rules

- Route collisions fail closed until the current BLE session ends.
- Disconnect clears queued input, active/pinned voice, diagnostic probe state,
  and link-local readiness exactly once while keeping persistent credentials.
- App close performs the same cleanup, then destroys the owned runtime
  resources in the existing order.
- A failed resource initialization leaves that optional resource unavailable;
  it does not weaken pairing or input authentication.
- UUIDs, crypto domains, nonce/replay guards, queue sizes, credential epochs,
  NVS contents, and normal no-backup flashing remain unchanged.

## Verification

- Installer behavior tests execute the source-only packaging path and check the
  packaged module set without duplicate manifest entries.
- Release contract tests inspect current build/flash behavior, not historical
  report prose.
- Route tests prove one shared registry inside a session, collision rejection,
  and recovery after reconnect.
- A focused runtime-session test proves idempotent disconnect and shutdown
  cleanup at the new seam; existing voice/input controller tests retain their
  detailed protocol coverage.
- Focused Python tests, relevant C++ host tests, syntax checks, `git diff
  --check`, and a firmware compile or the nearest available host compile gate
  close the work.

## Non-goals

- No status collection performance work.
- No completion-classifier changes.
- No full split of `AppCodexStatus`, `AgentRuntime`, or `TaskAlertTracker`.
- No protocol, UI, gesture, font, partition, signing, pairing, or flashing
  workflow redesign.
