# Agent-to-Watch Time Sync Design

## Goal

Synchronize the StopWatch clock from the Mac whenever StopWatch Codex Agent
establishes a BLE connection. The synchronized time must remain available to
all watch applications and survive leaving the Codex app or restarting the
device.

## Scope

- Send one time-sync event after every successful BLE connection, including
  reconnects.
- Synchronize the absolute current time and the Mac's current UTC offset.
- Update the watch system clock, persisted timezone setting, and RX8130 RTC.
- Keep the existing Codex status and alert behavior unchanged.

The first version synchronizes the current UTC offset, not an IANA timezone or
future daylight-saving transitions. A later connection refreshes the offset if
the Mac's effective timezone changes.

## Protocol

The Mac sends a dedicated `EVENT_MESSAGE` before the first status message:

```json
{"type":"time_sync","unix_ms":1783872000123,"utc_offset_minutes":480}
```

- `unix_ms` is the current Unix timestamp in milliseconds.
- `utc_offset_minutes` is the Mac's effective local offset from UTC and may
  include 30- or 45-minute increments.
- The watch accepts timestamps from 2020-01-01 through 2100-01-01 and offsets
  from -14 hours through +14 hours. Invalid payloads do not change time,
  timezone, or RTC state.

The event uses the existing encrypted BLE characteristic and framing protocol.
No new GATT service or characteristic is required.

## Mac Agent

`CodexStatusAgent` gains a focused `sync_time()` operation. It obtains the
timestamp and UTC offset immediately before serialization so scanning and
connection setup time do not make the value stale.

`AgentRuntime._run_session()` calls `sync_time()` after BLE reports connected
and before entering the regular two-second status loop. A write failure fails
the current BLE session and enters the existing reconnect path. Time is not
resent during a healthy connection.

Clock and timezone providers are injectable for deterministic tests.

## Watch Firmware

The Codex message parser recognizes `time_sync` as a separate message kind and
extracts validated 64-bit milliseconds plus a signed offset in minutes. Status
and chime parsing remain unchanged.

The Codex app dispatches a valid time-sync message to a HAL operation that:

1. Converts the offset to a POSIX timezone string and persists it through the
   existing system timezone setting.
2. Calls `settimeofday()` with second and microsecond precision.
3. Copies the resulting UTC system time into the RX8130 hardware RTC.

Because every watch face, alarm, and shared clock reads the process-wide system
time, the update is global. Persisting the timezone and RTC makes it survive
closing the Codex app and rebooting the watch.

## Failure Handling

- Invalid JSON, missing fields, fractional values, out-of-range timestamps, or
  out-of-range offsets are rejected before any state changes.
- All fields are validated before the HAL changes any state. A `settimeofday()`
  failure makes the handler return failure. Timezone persistence retains the
  existing settings layer's fail-fast NVS behavior.
- If the hardware RTC is unavailable, system time still updates; the HAL logs
  the RTC condition. This follows the existing RTC initialization behavior.
- A Mac-side BLE write failure closes the current session through the existing
  runtime error path and triggers a reconnect attempt.

## Verification

- Python tests assert one time-sync event per connection, correct timestamp and
  offset serialization, ordering before the first status payload, and resend on
  reconnect.
- Firmware parser tests cover valid messages and all invalid field/range cases.
- A pure timezone-formatting helper is tested with positive, negative,
  half-hour, and 45-minute offsets.
- The full Python suite and ESP-IDF firmware build must pass.
- Device validation checks that connecting updates the displayed time, leaving
  the Codex app preserves it, and rebooting restores it from RTC.
