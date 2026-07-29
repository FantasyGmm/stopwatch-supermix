# StopWatch Multi-Agent Secure Pairing

## Decision

Replace the build-time personal watch/Mac voice key with an application-layer
pairing protocol. One watch may authorize up to eight named Mac Agents, retain
one shared voice master key, reconnect to the most recently selected Agent by
default, and switch Agents from a pull-down panel in the Codex application.

The first firmware containing this feature still requires one normal firmware
update. After that update, adding or replacing a Mac Agent must not require a
watch rebuild or Flash write.

The watch permits one active Agent connection and one restricted pending
connection. A new or alternate Agent never interrupts the active connection
until the user explicitly approves a switch on the watch.

## Goals

- Pair a new Agent without compiling a personal key into firmware.
- Require a matching six-digit verification code and physical confirmation on
  the watch before authorizing a new Agent.
- Keep up to eight independently identifiable Agents on one watch.
- Share one 16-byte voice master key across authorized Agents while retaining
  per-Agent identity, naming, selection, and revocation.
- Allow the Agent name to be edited on the Mac and displayed on the watch.
- Automatically reconnect the most recently selected Agent when no other Agent
  is active.
- Rotate the shared master key after an Agent is removed, including safe
  catch-up for authorized Agents that were offline during rotation.
- Keep Codex status and time synchronization available when voice pairing,
  rekeying, or local voice prerequisites fail.

## Non-Goals

- Simultaneous status or voice use by multiple active Agents.
- Pairing while the Codex application is not in the foreground.
- Resistance to physical Flash extraction. ESP32 Flash and NVS encryption are
  separate hardening work.
- Cloud identity, account synchronization, or recovery through a remote
  service.
- Silent enrollment without physical watch confirmation.
- Backward-compatible voice operation with the build-time-key protocol.

## User Experience

### New Agent

1. The Mac Agent creates its long-term identity and asks the user for a display
   name.
2. The Agent opens the restricted pending BLE connection and submits a pairing
   request.
3. The Codex page shows a compact `New Agent: <name>` prompt without disturbing
   the active status page or voice session.
4. Pulling down opens the Agent panel. The watch and Mac display the same
   six-digit verification code.
5. The user compares the values and selects `Allow Pairing` on the watch.
6. The watch records the Agent identity and securely delivers the current
   master key and key epoch.
7. The new Agent remains pending until the user selects `Allow and Switch`.

Rejecting or ignoring the request leaves the current active connection
unchanged. A request expires after 60 seconds.

### Known Agent

An authorized Agent proves possession of its identity private key with a
challenge signature. It does not repeat the six-digit pairing flow.

- With no active connection, the most recently selected Agent becomes active
  automatically.
- An authorized Agent other than the active Agent appears as `Requesting
  Switch`.
- The user may approve the switch from the Agent panel.
- A switch first completes the existing bounded voice cleanup, including the
  stop Fn event and default-input restoration, before disconnecting the old
  Agent.

### Agent Panel

The Codex pull-down panel displays at most eight stored Agents:

```text
AGENTS

● Studio Mac          Connected
○ Office Mac          Requesting Switch
○ MacBook Pro         Offline
○ Test Agent          Offline

[Allow and Switch]
[Reject]
[Delete Agent]
```

A new request additionally displays its six-digit verification code and
`Allow Pairing` action. Destructive removal requires a confirmation step.
`Reset All Agents` requires a separate second confirmation and is visually
separated from ordinary switching.

### Naming

The Mac menu adds `Agent Name...`. A name:

- is normalized to one line;
- cannot be empty;
- contains no control characters;
- is limited to 24 UTF-8 bytes without splitting a code point.

The Agent signs every name update. The watch updates only the matching
authorized identity. Offline changes are queued locally and synchronized on
the next authenticated connection. Duplicate names are allowed; the watch
adds a four-hex-digit identity suffix when disambiguation is required.

## Architecture

### Watch

`AgentRegistry` owns persistent identity and key state:

- stable random `watch_id`;
- 16-byte voice master key;
- monotonically increasing nonzero `key_epoch`;
- up to eight `AgentRecord` values;
- the identity of the most recently selected Agent;
- a registry generation and integrity/version marker.

`SecurePairingSession` owns one in-memory pending transaction:

- connection handle;
- transaction identifier;
- Agent display name and identity public key;
- both ephemeral public keys and nonces;
- derived pairing key;
- verification code;
- deadline and failure count.

`AgentConnectionRouter` owns connection roles:

- at most one `Active` connection;
- at most one `Pending` connection;
- per-handle authorization and GATT access;
- promotion, rejection, disconnect, and active-session cleanup.

The BLE transport must move from one global connection handle to explicit
per-connection state. Production status, time, and voice characteristics are
available only to the active authorized handle. Pairing characteristics are
available only to the pending handle.

`CONFIG_BT_NIMBLE_MAX_CONNECTIONS` becomes `2`. The change is accepted only if
the clean unified build, memory gates, Codex lifecycle tests, and physical
Assistant recovery remain within their existing limits.

### Mac Agent

`AgentIdentityStore` owns the long-term P-256 identity private key and display
name in Keychain.

`WatchCredentialStore` stores one record per `watch_id`:

- shared master key;
- current key epoch;
- watch display metadata;
- identity fingerprint last accepted by the watch.

`PairingClient` implements new enrollment and known-Agent authentication.

`WatchConnectionManager` keeps the existing single active application session
while allowing one restricted pending request. It never starts status or voice
services on a pending connection.

The menu exposes:

- Agent name and rename action;
- paired watch entries;
- pending verification code and state;
- retry or forget actions for local pairing failures.

## Persistent Data

### Watch NVS

Use a dedicated versioned namespace instead of the build-generated voice key.
The logical record is:

```text
registry_version
registry_generation
watch_id
master_key
key_epoch
last_agent_fingerprint
agent_count
agent[0..7]:
  identity_public_key
  fingerprint
  display_name
  last_connected_at
```

The exact physical encoding may use fixed slots and compact blobs, but updates
must be copy-on-write or transactionally staged. A power loss must yield either
the old complete registry or the new complete registry, never a partially
rotated key with an old epoch.

The master key is generated on the watch with the ESP hardware random source.
It is not generated at build time and is never logged.

### macOS Keychain

Keychain records are separated by purpose:

- one Agent identity private key;
- one display-name record;
- one credential record per `watch_id`.

The key value never appears in command-line arguments, application resources,
preferences, logs, reports, or menu snapshots. Updating a watch credential is
atomic from the Agent's perspective: the new key and epoch replace the old pair
together.

## Cryptographic Protocol

### Algorithms

- Identity signing: ECDSA P-256 with SHA-256.
- Ephemeral key agreement: ECDH P-256.
- Key derivation: HKDF-SHA256.
- Key wrapping: AES-128-CCM with an 8-byte tag.
- Voice encryption: the existing AES-128-CCM construction, extended with a key
  epoch in authenticated data.

Public keys use a canonical compressed representation. Decoders validate curve
membership, lengths, reserved fields, and protocol version before performing
cryptographic operations.

### Pairing Transcript

Both sides generate fresh nonzero random values and ephemeral key pairs. The
canonical transcript binds:

- protocol version and transaction identifier;
- `watch_id`;
- watch and Agent random values;
- watch and Agent ephemeral public keys;
- Agent identity public key;
- normalized Agent display name.

ECDH followed by HKDF derives separate keys for:

- transcript confirmation;
- master-key wrapping;
- final pairing acknowledgement.

The six-digit verification code is derived from the transcript confirmation
key and canonical transcript. It is a short authentication string, not key
material. The watch does not persist the Agent or deliver the master key until
the user confirms the matching code.

After confirmation, the watch commits the Agent record, wraps
`master_key || key_epoch || watch_id || Agent fingerprint`, and sends it over
the pending connection. The Agent writes the credential to Keychain and
returns an authenticated acknowledgement. Only then may the record be treated
as fully paired.

If Keychain storage or acknowledgement fails, the watch marks no active
session for that Agent. A safely committed identity may retry credential
delivery through the known-Agent authentication flow.

### Known-Agent Authentication

The watch sends a fresh challenge containing:

- `watch_id`;
- connection nonce;
- current `key_epoch`;
- connection role and protocol version.

The Agent signs the canonical challenge with its identity private key. The
watch verifies the signature against the matching registry public key. Shared
master-key possession alone is never accepted as identity proof.

After identity verification:

- an Agent with the current epoch may proceed;
- an authorized Agent with an older epoch enters rekey before any voice
  control is accepted;
- an unknown or removed identity is rejected even if it possesses an old
  master key.

### Key Rotation

Deleting one Agent performs this order:

1. Remove its identity from the registry.
2. Generate a new random master key.
3. Increment `key_epoch`.
4. Atomically commit the new registry.
5. Immediately reject the old epoch.
6. Rekey the current active Agent if it is still authorized.

Revocation takes priority over uninterrupted voice. If immediate rekey fails,
voice becomes unavailable while status and time synchronization remain active.

An offline authorized Agent updates on its next connection:

1. It proves identity with a challenge signature.
2. The watch observes its stale epoch.
3. The watch creates a fresh ephemeral ECDH key pair and combines it with the
   stored Agent identity public key.
4. HKDF derives an Agent-specific wrapping key.
5. The watch wraps the current master key and epoch.
6. The Agent uses its Keychain identity private key to derive the same wrapping
   key, decrypts the credential, and atomically updates Keychain.

The watch does not need to persist one envelope per offline Agent. Envelopes
are generated on demand from the current master key and each stored public key.

Rotation protects future traffic. It cannot make a removed Agent forget old
keys or prevent decryption of traffic it recorded before revocation.

### Voice Epoch

Production voice advances to a new protocol version. Every READY, UNAVAILABLE,
PTT_BEGIN, AUDIO, PTT_END, and VOICE_ERROR message includes `key_epoch` in its
clear authenticated header. Both sides require exact equality with the
currently committed epoch before accepting authenticated plaintext.

There is no fallback to the build-time key or the previous production protocol
version.

## Connection And UI State

The connection router has these roles:

```text
Disconnected
PendingUnknown
PendingVerification
PendingPaired
ActiveAuthenticating
ActiveRekeying
ActiveReady
Rejecting
```

Only `ActiveReady` may use status, time, or voice characteristics.
`ActiveAuthenticating` and `ActiveRekeying` remain fail-closed for voice.

The foreground Codex page owns enrollment prompts and the Agent panel. Leaving
Codex:

- cancels an unknown pending pairing request;
- disconnects the pending handle;
- performs existing active voice cleanup;
- preserves the Agent registry.

## Abuse Controls

- Only one pending request exists at a time.
- A pending request expires after 60 seconds.
- Three failed or rejected verification attempts cause a 60-second cooldown.
- Invalid packets do not refresh the deadline.
- Unknown Agents cannot read the registry, active Agent name, status payload,
  voice state, or master-key epoch beyond what is necessary to run pairing.
- A pending connection cannot subscribe to production notifications.
- Names and public inputs are length checked before allocation or UI dispatch.
- All secrets and raw cryptographic packets are excluded from logs.

## Failure Handling

- Registry full: reject enrollment and ask the user to delete an Agent.
- Duplicate identity: authenticate it as a known Agent; never consume a second
  slot.
- Invalid or mismatched verification code: clear the pending transaction and
  disconnect.
- User rejection or timeout: clear ephemeral secrets and leave Active
  untouched.
- NVS failure before commit: fail enrollment or rotation without delivering
  the candidate key.
- Keychain failure: keep status available, report an actionable Agent error,
  and retry only through authenticated pairing/rekey.
- Registry corruption: disable voice and present `Reset All Agents`; do not
  silently generate a replacement key.
- Rekey failure: keep status and time available, reject voice, and retry on the
  next authenticated connection.
- Active disconnect during a pending request: the pending Agent does not become
  active without an explicit switch unless it is already the last selected
  authorized Agent.
- Switch during recording: finish bounded audio drain, stop Typeless, restore
  the previous input, then disconnect.
- Power loss during registry update: restore the last complete committed
  generation.

## Migration

The first firmware containing this design initializes a new registry:

- If no registry exists, create `watch_id`, master key, and epoch `1`.
- Do not import the old build-time key into NVS.
- Existing Mac Keychain build-time credentials are ignored by the new protocol.
- The first Agent must use the six-digit enrollment flow.

The generated firmware key header and mandatory external key-file build gate
are removed after migration. Diagnostic probe keys remain isolated and
unchanged.

The installed Agent may retain cleanup code for the legacy fixed Keychain item
for one release, but it must never use that item to authenticate production
voice.

## Testing

### Host Unit Tests

- Canonical transcript encoding and cross-language byte identity.
- P-256 ECDH, HKDF, ECDSA, wrapping, and tamper rejection vectors.
- Six-digit code equality and sensitivity to every transcript field.
- Agent name normalization, UTF-8 truncation, duplicate-name suffixes, and
  signed rename validation.
- Registry capacity, duplicate identity, last-selected behavior, deletion, and
  generation recovery.
- Rotation invalidates old epochs and preserves all remaining identities.
- Offline authorized Agent rekeys successfully; removed Agent does not.
- Keychain records are isolated by `watch_id` and updated atomically.
- No key, private identity, plaintext wrapper, or raw packet enters logs or
  snapshots.

### Firmware Contract And Native Tests

- NimBLE supports exactly two connections for this transport.
- Production GATT access is restricted to the active handle.
- Pairing GATT access is restricted to the pending handle.
- Pending requests cannot alter current status or voice queues.
- Timeout, cooldown, and foreground ownership are deterministic.
- Registry writes survive injected failures at every staging boundary.
- Voice protocol rejects old versions, old epochs, wrong identities, replayed
  challenges, malformed keys, and modified tags.
- Agent panel layout fits the round screen for eight records and all
  confirmation states.

### Integration And Hardware Acceptance

- Pair two differently named Mac Agents to one clean watch.
- Keep Agent A active while Agent B requests pairing; verify A remains usable.
- Confirm matching codes and reject mismatched or timed-out requests.
- Switch A to B during idle and during active push-to-talk cleanup.
- Rename both online and offline Agents and verify synchronized display.
- Reboot the watch and both Macs; verify last-Agent reconnection.
- Remove one Agent, observe epoch rotation, rekey the online Agent, and later
  rekey an offline authorized Agent.
- Verify the removed Agent cannot authenticate, obtain the new key, or send
  accepted voice packets.
- Fill all eight slots and verify the ninth request fails without overwriting.
- Exercise `Reset All Agents` and confirm every old identity and epoch is
  rejected.
- Run Assistant recovery and existing mixed lifecycle gates with two NimBLE
  connections configured.
- Verify intelligible Mandarin voice input, zero accepted invalid tags, no
  sequence regression, and restoration of the previous default input.

## Acceptance Criteria

- A newly installed Agent pairs without a personalized firmware rebuild.
- Pairing requires matching six-digit codes and a physical watch confirmation.
- One active Agent remains uninterrupted while another requests access.
- The watch stores and displays up to eight named Agent identities.
- The last selected Agent reconnects automatically when the watch has no active
  Agent.
- Agent switching is controlled only from the watch pull-down panel.
- Removing an Agent rotates the master key and prevents that identity from
  receiving future epochs.
- Offline authorized Agents update securely on their next connection.
- Status and time synchronization remain available when voice is blocked.
- All automated gates and physical acceptance scenarios pass without exceeding
  firmware, internal-memory, or BLE stability limits.
