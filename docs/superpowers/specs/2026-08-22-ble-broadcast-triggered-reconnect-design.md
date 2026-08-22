# BLE Broadcast-Triggered Reconnect Design

## Goal

Reduce watch reconnection latency without keeping an application-level polling
loop active or adding a second BLE discovery protocol.

## Decision

The watch remains a connectable advertiser. While disconnected and enabled,
the Mac Agent keeps one CoreBluetooth scan registered until the matching watch
advertisement is observed. It then stops scanning and follows the existing
connect, authenticate, and subscribe path.

An unexpected scan completion without a match restarts listening immediately
instead of entering the general connection-error backoff. Real connection or
Bluetooth errors retain the bounded backoff. A successfully connected session
resets old retry history so a later disconnect starts from the shortest delay.

Automatic authentication of an already-authorized watch uses the normal GATT
operation timeout. The longer confirmation timeout remains limited to first
pairing, where a person must approve the code on the watch.

## Scope and Safety

This is an Agent-only lifecycle change. It does not change firmware,
advertising parameters, service UUIDs, credentials, replay protection, or
pairing confirmation behavior. Disabling, reconnecting, or stopping the Agent
cancels the outstanding scan through the existing session cancellation path.

## Verification

- A transport test proves discovery has no periodic timeout gap.
- An authentication test proves authorized reconnect uses the normal GATT
  timeout.
- Runtime tests prove a no-match result relistens immediately and a successful
  session clears prior retry escalation.
- Existing connect, reconnect, stop-during-scan, pairing, and installer tests
  remain green.
- The installed Agent must be restarted and a real watch reconnect confirmed
  from live logs.

## Self-review

The design addresses the measured delay sources directly: the 30-second deaf
gap and the misplaced 65-second authentication timeout. It does not add dual
scanning, firmware messages, wake polling, or new state machines. Remaining
latency is the existing macOS BLE discovery, connection, GATT authentication,
and subscription path.
