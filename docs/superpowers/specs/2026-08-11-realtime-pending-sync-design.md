# Real-Time Pending Sync Design

## Goal

When a Codex turn stops and becomes Pending, update the connected watch and play
the alert sound as soon as the local completion record is durable. Normal-path
latency should be dominated by local file notification and BLE transfer rather
than the existing two-second polling cadence.

## Constraints

- The watch must never chime for a newer ledger state while displaying an older
  Pending count.
- A failed BLE write must not acknowledge the alert; reconnect must retain the
  existing one-summary-chime behavior.
- Polling remains as a recovery path for missed filesystem notifications.
- No prompt, response body, tool data, or raw rollout record may be sent to the
  watch.
- The design must reuse the current status protocol and avoid a second task
  ledger or a general-purpose event bus.

## Chosen Architecture

Use an event-driven fast path with the existing two-second heartbeat as a
fallback.

`TaskAlertTracker` exposes a bounded wait for changes to the state database and
known rollout files. On macOS it uses the standard-library `kqueue` vnode
filter, so no new runtime dependency is required. The wait is armed from the
tracker's current cursor set and also checks file signatures before and after
registration, preventing an append between refresh and watcher setup from being
lost. Unsupported platforms simply retain the timeout path.

`CodexStateCollector.get_state()` derives the unnotified Pending IDs from the
same immutable `TaskAlertSnapshot` used to build counts and rows. Those IDs are
stored under a private status key that the watch serializer never transmits.
The runtime publishes each collected status with a monotonic generation and
wakes the BLE session immediately. Only the Pending IDs carried by that exact
generation may produce its chime.

The BLE sender keeps one serialized writer. It sends the complete status first,
then the chime, and acknowledges the IDs only after both writes succeed. Status
framing uses the connected characteristic's negotiated safe write length,
bounded by the watch's existing 256-byte frame capacity, with the current
20-byte size as a compatibility fallback. This removes most per-fragment GATT
round trips without changing the firmware protocol.

## Data Flow

1. Codex appends a complete `task_complete` JSONL record.
2. The vnode watcher wakes the collector immediately.
3. The tracker reduces the record and returns one snapshot containing counts,
   rows, revision, and unnotified Pending IDs.
4. The collector publishes a new runtime generation.
5. The BLE loop wakes without waiting for its heartbeat timeout.
6. The sender transmits the full status using negotiated-size frames.
7. After the status write succeeds, the sender transmits the chime.
8. After the chime succeeds, the tracker persists `watch_notified=true` for the
   IDs from that same generation.

If another completion arrives during steps 6–8, it produces a newer generation
and is sent immediately after the current serialized write. It cannot be folded
into the older chime decision.

## Failure Handling

- Filesystem notification missed: the two-second timeout performs the normal
  refresh and converges.
- Partial JSONL record: the cursor does not advance; the next vnode event or
  fallback refresh retries it.
- Status write fails: no chime is sent and no alert is acknowledged.
- Chime write fails: no alert is acknowledged, so reconnect retries the summary
  notification.
- BLE disconnected: collection continues; the latest snapshot and all
  unacknowledged Pending IDs are available on reconnect.
- Negotiated write length unavailable or invalid: framing falls back to the
  validated 20-byte path.

## Scope

The implementation changes only the Mac Agent collection, runtime scheduling,
BLE framing, tests, and operator documentation. The watch parser and visual
layout remain unchanged because they already render a complete status message
immediately and the existing frame reassembler accepts frames up to 256 bytes.

## Verification

- A deterministic race test proves that a live ledger update cannot chime from
  a stale provided status.
- Runtime tests prove a collector change wakes BLE before the heartbeat and
  coalesces multiple rapid changes to the latest generation.
- Watcher tests prove append wakeup, timeout fallback, and unsupported-platform
  fallback.
- Transport tests prove negotiated framing reduces writes, stays within 256
  bytes, and preserves the 20-byte fallback.
- Existing alert persistence, reconnect, voice, and secure-pairing suites remain
  green.
- Physical validation records completion-to-watch latency and confirms the
  Pending number is visible no later than the chime.
