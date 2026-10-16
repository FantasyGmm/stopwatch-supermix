# Binary status payloads over the existing BLE link

New peers use MessagePack for status, telemetry and task-detail payloads. The
existing JSON format remains available to old Agents and firmware. Collection
and send intervals do not change: task collection and the BLE send tick remain
two seconds; usage and quota collection retain their existing cadence.

## Negotiate once per authenticated connection

The existing status RX characteristic (`…0002`) retains its WRITE properties
and gains READ. There is no new characteristic, service, Wi-Fi manager or
hardware owner. Both READ and WRITE use the existing Production GATT
authorization check.

After authentication and credential consistency checks, the Agent examines
the discovered RX properties:

- No READ property means legacy firmware: use JSON framing v1.
- With READ, read and validate the eight-byte capability descriptor below.
  Prefer MessagePack v2 if offered; otherwise use offered JSON v1.
- A READ failure or invalid descriptor is an explicit connection error, not
  evidence of legacy firmware. A short, bounded retry is allowed because the
  existing AUTH_OK notification can precede Production-session promotion.
- Keep the choice only in the current connection. Reconnect negotiates again.

| Byte | Meaning |
| --- | --- |
| 0–2 | ASCII `SWC` (`53 57 43` in hex) |
| 3 | Descriptor version: `1` |
| 4 | Codec bits: bit 0 JSON v1, bit 1 MessagePack v2; current firmware offers `03` |
| 5 | Reserved: `0` |
| 6–7 | Maximum complete payload, uint16 little-endian: `2048` (`00 08`) |

Unknown codec bits are ignored; at least one known codec must be offered. This
descriptor version requires the 2048-byte limit. Other descriptor versions,
reserved values or limits are rejected explicitly.

## Keep the six-byte frame header

| Byte | Meaning |
| --- | --- |
| 0 | `1` for JSON, `2` for MessagePack |
| 1 | Existing message type: status `1`, event `2` |
| 2–3 | Existing sequence, uint16 little-endian |
| 4 | Zero-based fragment index |
| 5 | Fragment count, 1–255 |

The payload is a slice of the selected encoding, not another envelope. Keep
the existing MTU-derived capacity, sequential write acknowledgments, burst
pause, receive queue bounds, 2048-byte message limit and three-second assembly
timeout. One assembly must retain the same version, type, sequence and fragment
count. A fragment cannot switch codec mid-message.

Old public encoding APIs retain JSON defaults. Add explicit codec-aware APIs
for the negotiated BLE path. The local WebSocket path and legacy Python client
remain JSON; existing callers do not acquire a new negotiation prerequisite.

## Use the existing public fields and actual encoded byte budget

MessagePack status, inventory, telemetry and event objects use the same public
string keys, values, null semantics and version fields as JSON. Do not serialize
the collector's private snapshot, credentials, notification IDs, prompts or
private `_ui_*` data. Preserve whitelist projection and existing model/task
count limits.

The complete encoded payload must fit its existing limit (2048 bytes, or the
existing 1024-byte TRAE projection limit). Apply trimming to the selected
encoding rather than limiting MessagePack by its hypothetical JSON size. Keep
the usage `more` count and partial/unknown semantics when rows are omitted.

The receiver uses the pinned ArduinoJson MessagePack parser. Only one complete
top-level map is accepted: reject trailing bytes, excessive nesting, non-string
map keys, unsupported extension values and invalid business values. Bound
decoded nodes/strings as well as input bytes. Preserve exact integer bounds;
do not silently round uint64 values through a double.

Decode binary once into the typed model or a bounded cJSON-object adapter used
by the existing validators. Do not serialize MessagePack back to JSON text and
parse that text again. Legacy JSON wrappers remain available and use the same
business validation/application paths.

## Send encrypted task details as native bytes

Task-detail MessagePack maps retain these existing fields:

| Field | MessagePack representation |
| --- | --- |
| `type` | String `task_detail` |
| `route` | Existing nonzero lowercase 16-digit hex string |
| `revision`, `index`, `count`, `epoch`, `sequence` | Existing bounded unsigned integers |
| `nonce` | Native `bin`, exactly 8 bytes |
| `ciphertext` | Native `bin`, the existing ciphertext plus authentication tag |

The legacy JSON envelope still uses hexadecimal nonce and Base64 ciphertext.
Only their external representation changes. Reuse the same typed envelope,
AES-CCM domain, nonce, authentication tag, canonical AAD bytes, replay checks,
chunk order and detail store. Canonical AAD may still be generated locally in
its existing JSON form; it is not sent as JSON over BLE. Keep 1300-byte
plaintext chunks, at most 16 chunks and the existing 12 KiB reply bound.

Native binary values are accepted only for the two defined task-detail fields;
ordinary public status values retain JSON-compatible types. A malformed binary
envelope must not bypass authentication, advance application state or cause a
retry of an ambiguous input action.

## Preserve the independent channels

Pairing, credential lifecycle, input actions, voice/ADPCM and pet transfer keep
their existing binary formats and UUIDs. No OAuth tokens or cookies are added.
The status codec choice must not reset voice, input, credentials or user
preferences. Disconnect and epoch changes still retire the existing session.
