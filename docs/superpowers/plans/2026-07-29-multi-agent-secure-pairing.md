# StopWatch Multi-Agent Secure Pairing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let one StopWatch securely enroll, name, remember, switch, rekey, and revoke up to eight Mac Agents without a personalized firmware rebuild.

**Architecture:** Add a versioned application-layer P-256 pairing protocol, a transactional watch-side Agent registry, and an Active/Pending two-connection BLE router. Each Mac keeps an independent identity private key while authorized Agents share the watch-generated AES voice key; identity signatures authorize on-demand key delivery and rekeying.

**Tech Stack:** ESP-IDF 5.5.4, NimBLE, C++17, mbedTLS P-256/ECDSA/ECDH/HKDF/AES-CCM, ESP-IDF NVS, Python 3.9, `cryptography`, PyObjC/Keychain, LVGL 9, Python `unittest`, native C++ host tests.

---

## File Structure

New watch files:

- `main/apps/app_codex_status/agent_registry.h/.cpp` — pure registry rules and versioned records.
- `main/apps/app_codex_status/agent_registry_store.h/.cpp` — transactional NVS persistence.
- `main/apps/app_codex_status/secure_pairing_crypto.h/.cpp` — P-256, HKDF, verification-code, signature, and key-wrap primitives.
- `main/apps/app_codex_status/secure_pairing_protocol.h/.cpp` — canonical pairing/auth/rekey wire messages.
- `main/apps/app_codex_status/secure_pairing_session.h/.cpp` — one bounded pending transaction.
- `main/apps/app_codex_status/agent_connection_router.h/.cpp` — Active/Pending handle ownership and promotion.
- `main/apps/app_codex_status/agent_panel_model.h/.cpp` — UI-safe Agent rows, prompts, and actions.

New Mac files:

- `mac-codex-agent/agent_identity.py` — identity Keychain storage, names, signatures, and public identity.
- `mac-codex-agent/watch_credentials.py` — per-`watch_id` master-key/epoch Keychain records.
- `mac-codex-agent/secure_pairing_crypto.py` — Python cross-language protocol crypto.
- `mac-codex-agent/secure_pairing_protocol.py` — canonical pairing/auth/rekey encoding.
- `mac-codex-agent/pairing_client.py` — enrollment, known-Agent auth, and rekey state machine.

Existing integration files:

- `main/apps/app_codex_status/codex_ble_transport.h/.cpp`
- `main/apps/app_codex_status/app_codex_status.h/.cpp`
- `main/apps/app_codex_status/voice_input_crypto.h/.cpp`
- `main/apps/app_codex_status/voice_input_protocol.h/.cpp`
- `main/apps/app_codex_status/voice_input_controller.h/.cpp`
- `mac-codex-agent/agent.py`
- `mac-codex-agent/runtime.py`
- `mac-codex-agent/app_state.py`
- `mac-codex-agent/menu_app.py`
- `sdkconfig.defaults`
- `main/CMakeLists.txt`
- `scripts/run_unified_offline_validation.sh`
- `mac-codex-agent/install_launch_agent.sh`
- `README.md`
- `mac-codex-agent/README.md`

## Task 1: Canonical Pairing Protocol And Cross-Language Crypto

**Files:**
- Create: `mac-codex-agent/secure_pairing_crypto.py`
- Create: `mac-codex-agent/secure_pairing_protocol.py`
- Create: `main/apps/app_codex_status/secure_pairing_crypto.h`
- Create: `main/apps/app_codex_status/secure_pairing_crypto.cpp`
- Create: `main/apps/app_codex_status/secure_pairing_protocol.h`
- Create: `main/apps/app_codex_status/secure_pairing_protocol.cpp`
- Create: `mac-codex-agent/tests/test_secure_pairing_crypto.py`
- Create: `tests/secure_pairing_crypto_test.cpp`
- Create: `tests/secure_pairing_protocol_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write failing Python vector tests**

Define fixed test-only P-256 private scalars, nonces, `watch_id`, Agent name,
identity key, and ephemeral keys. Assert byte-identical canonical transcript,
six-digit code, ECDH/HKDF output, signature verification, wrapped credential,
and rejection after changing every bound field.

```python
def test_pairing_vector_and_tamper_rejection(self):
    transcript = encode_pairing_transcript(FIXTURE)
    self.assertEqual(transcript.hex(), EXPECTED_TRANSCRIPT_HEX)
    self.assertEqual(verification_code(CONFIRM_KEY, transcript), "482731")
    wrapped = wrap_credential(WRAP_KEY, FIXTURE.wrap_nonce, CREDENTIAL, transcript)
    self.assertEqual(wrapped.hex(), EXPECTED_WRAPPED_HEX)
    self.assertEqual(
        unwrap_credential(WRAP_KEY, FIXTURE.wrap_nonce, wrapped, transcript),
        CREDENTIAL,
    )
    with self.assertRaises(InvalidTag):
        unwrap_credential(WRAP_KEY, FIXTURE.wrap_nonce, wrapped, transcript[:-1] + b"x")
```

- [ ] **Step 2: Run Python tests and verify RED**

Run:

```bash
PYTHONPATH=mac-codex-agent python3 -m unittest \
  mac-codex-agent.tests.test_secure_pairing_crypto -v
```

Expected: import failure for `secure_pairing_crypto`.

- [ ] **Step 3: Implement the Python protocol primitives**

Expose these exact public boundaries:

```text
PAIRING_VERSION: int = 1
IDENTITY_CURVE: ec.SECP256R1

PairingTranscript(
    transaction_id: int,
    watch_id: bytes,
    watch_nonce: bytes,
    agent_nonce: bytes,
    watch_ephemeral_public: bytes,
    agent_ephemeral_public: bytes,
    agent_identity_public: bytes,
    agent_name: str,
)

encode_pairing_transcript(PairingTranscript) -> bytes
derive_pairing_keys(shared_secret: bytes, transcript: bytes) -> PairingKeys
verification_code(confirm_key: bytes, transcript: bytes) -> str
sign_challenge(private_key, challenge: bytes) -> bytes
verify_challenge(public_key: bytes, challenge: bytes, signature: bytes) -> bool
wrap_credential(key: bytes, nonce: bytes, plaintext: bytes, aad: bytes) -> bytes
unwrap_credential(key: bytes, nonce: bytes, ciphertext: bytes, aad: bytes) -> bytes
```

Use length-prefixed canonical fields, compressed P-256 public keys, HKDF-SHA256
labels `stopwatch/pair/confirm`, `stopwatch/pair/wrap`, and
`stopwatch/pair/ack`, and AES-CCM with an 8-byte tag.

- [ ] **Step 4: Write and run failing C++ vector tests**

Add the same fixed vectors to both host tests and register exact compile/run
pairs in `scripts/run_unified_offline_validation.sh`.

Expected: compilation fails because the C++ headers do not exist.

- [ ] **Step 5: Implement mbedTLS equivalents and verify GREEN**

Implement fixed-size, allocation-bounded APIs matching the Python byte layout.
Reject non-canonical keys, zero nonces, oversized names, unknown versions, and
trailing bytes before crypto. Run both Python and native test pairs.

- [ ] **Step 6: Commit**

```bash
git add mac-codex-agent/secure_pairing_crypto.py \
  mac-codex-agent/secure_pairing_protocol.py \
  mac-codex-agent/tests/test_secure_pairing_crypto.py \
  main/apps/app_codex_status/secure_pairing_crypto.* \
  main/apps/app_codex_status/secure_pairing_protocol.* \
  tests/secure_pairing_crypto_test.cpp tests/secure_pairing_protocol_test.cpp \
  scripts/run_unified_offline_validation.sh
git commit -m "feat: add secure Agent pairing protocol"
```

## Task 2: Watch Agent Registry And Transactional NVS Store

**Files:**
- Create: `main/apps/app_codex_status/agent_registry.h`
- Create: `main/apps/app_codex_status/agent_registry.cpp`
- Create: `main/apps/app_codex_status/agent_registry_store.h`
- Create: `main/apps/app_codex_status/agent_registry_store.cpp`
- Create: `tests/agent_registry_test.cpp`
- Create: `mac-codex-agent/tests/test_agent_registry_firmware_contract.py`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write failing registry behavior tests**

Cover initialization, eight-slot capacity, duplicate identity, UTF-8-safe
24-byte names, last-selected identity, deletion, epoch increment, master-key
rotation, corruption, and recovery of the last complete generation.

```cpp
AgentRegistry registry = AgentRegistry::initialize(entropy);
CHECK(registry.key_epoch() == 1);
CHECK(registry.add(agent("Studio Mac")));
CHECK(!registry.add(agent_with_same_identity("Duplicate")));
for (int i = 1; i < 8; ++i) CHECK(registry.add(unique_agent(i)));
CHECK(!registry.add(unique_agent(9)));
const auto old_key = registry.master_key();
CHECK(registry.remove_and_rotate(registry.records()[2].fingerprint, entropy));
CHECK(registry.key_epoch() == 2);
CHECK(registry.master_key() != old_key);
```

- [ ] **Step 2: Run tests and verify RED**

Run the new native pair and:

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_agent_registry_firmware_contract -v
```

Expected: missing registry files and NVS contract.

- [ ] **Step 3: Implement the pure registry**

Use fixed-capacity records:

```cpp
constexpr size_t kMaxAgents = 8;
constexpr size_t kAgentNameBytes = 24;

struct AgentRecord {
    IdentityPublicKey public_key;
    AgentFingerprint fingerprint;
    std::array<char, kAgentNameBytes + 1> display_name;
    int64_t last_connected_at;
};
```

Keep master-key mutation inside `initialize`, `remove_and_rotate`, and
`reset_all`; return explicit result enums for Full, Duplicate, InvalidName,
NotFound, and EntropyFailure.

- [ ] **Step 4: Implement two-generation NVS persistence**

Use namespace `codex_agents`, slots `registry_a` and `registry_b`, plus a small
committed-generation selector. Serialize a version, generation, length, data,
and SHA-256 integrity value. Write and verify the inactive slot before changing
the selector. Never erase the working slot before the new slot is committed.

- [ ] **Step 5: Verify GREEN and commit**

Run the registry native tests, contract test, and NVS-related offline tests.

```bash
git add main/apps/app_codex_status/agent_registry* \
  tests/agent_registry_test.cpp \
  mac-codex-agent/tests/test_agent_registry_firmware_contract.py \
  scripts/run_unified_offline_validation.sh
git commit -m "feat: persist authorized Agent registry"
```

## Task 3: Runtime Voice Key And Epoch

**Files:**
- Modify: `main/apps/app_codex_status/voice_input_crypto.h`
- Modify: `main/apps/app_codex_status/voice_input_crypto.cpp`
- Modify: `main/apps/app_codex_status/voice_input_protocol.h`
- Modify: `main/apps/app_codex_status/voice_input_protocol.cpp`
- Modify: `main/apps/app_codex_status/voice_input_controller.h`
- Modify: `main/apps/app_codex_status/voice_input_controller.cpp`
- Modify: `mac-codex-agent/voice_input_crypto.py`
- Modify: `mac-codex-agent/voice_input_protocol.py`
- Modify: `mac-codex-agent/voice_session.py`
- Modify: `mac-codex-agent/tests/test_voice_input_crypto.py`
- Modify: `mac-codex-agent/tests/test_voice_input_protocol.py`
- Modify: `tests/voice_input_crypto_test.cpp`
- Modify: `tests/voice_input_protocol_test.cpp`

- [ ] **Step 1: Extend tests for runtime keys and exact epochs**

Assert that contexts accept runtime keys, every production packet authenticates
a nonzero `key_epoch`, old epochs fail before state mutation, and the old
protocol version is rejected.

```python
packet = encode_ready(KEY, key_epoch=7, exchange_nonce=NONCE, target_bps=8192)
self.assertEqual(decode_control(KEY, expected_epoch=7, packet=packet).key_epoch, 7)
with self.assertRaises(InvalidTag):
    decode_control(KEY, expected_epoch=8, packet=packet)
```

- [ ] **Step 2: Run focused tests and verify RED**

Run Python voice crypto/protocol tests and the two native equivalents.
Expected: existing APIs have no epoch parameter and firmware uses
`generated::kDeviceKey`.

- [ ] **Step 3: Implement runtime key ownership**

Remove `device_context()`. Construct or replace `CcmContext` only from the
registry's current key and epoch. Zero retired key bytes, finish active voice
cleanup before replacement, and expose:

```cpp
bool VoiceInputController::set_credential(
    const voice_input::crypto::Key& key,
    uint32_t key_epoch);
void VoiceInputController::clear_credential();
```

Add `key_epoch:u32 little-endian` to every production control and notification
AAD header and bump the production protocol version. Decode requires exact
epoch equality.

- [ ] **Step 4: Verify GREEN and commit**

```bash
git add main/apps/app_codex_status/voice_input_* \
  mac-codex-agent/voice_input_crypto.py \
  mac-codex-agent/voice_input_protocol.py \
  mac-codex-agent/voice_session.py \
  mac-codex-agent/tests/test_voice_input_crypto.py \
  mac-codex-agent/tests/test_voice_input_protocol.py \
  tests/voice_input_crypto_test.cpp tests/voice_input_protocol_test.cpp
git commit -m "feat: use runtime voice key epochs"
```

## Task 4: Pending Pairing Session And Known-Agent Authentication

**Files:**
- Create: `main/apps/app_codex_status/secure_pairing_session.h`
- Create: `main/apps/app_codex_status/secure_pairing_session.cpp`
- Create: `tests/secure_pairing_session_test.cpp`
- Create: `mac-codex-agent/pairing_client.py`
- Create: `mac-codex-agent/tests/test_pairing_client.py`
- Modify: `scripts/run_unified_offline_validation.sh`

- [ ] **Step 1: Write failing state-machine tests**

Test request, code display, watch confirmation, registry commit, wrapped-key
delivery, acknowledgement, known identity challenge, stale-epoch rekey,
removed identity rejection, timeout, three-failure cooldown, and cleanup.

```cpp
session.request(valid_request(), now);
CHECK(session.state() == PairingState::AwaitingConfirmation);
CHECK(session.verification_code() == "482731");
CHECK(session.confirm(registry, store));
CHECK(session.state() == PairingState::AwaitingCredentialAck);
session.ack(valid_ack());
CHECK(session.state() == PairingState::Paired);
```

- [ ] **Step 2: Run tests and verify RED**

Expected: missing `SecurePairingSession` and `PairingClient`.

- [ ] **Step 3: Implement the watch state machine**

Use one fixed pending record, monotonic deadlines, explicit state transitions,
and secret-zeroing cleanup. NVS commit must precede credential delivery.
Invalid traffic never extends the deadline.

- [ ] **Step 4: Implement the Mac state machine**

Expose:

```text
PairingClient.request_pairing(
    transport, identity, display_name: str
) -> PairingPresentation
PairingClient.confirm_pairing(accepted: bool) -> PairingResult
PairingClient.authenticate(
    transport, identity, credential
) -> AuthenticationResult
PairingClient.accept_rekey(envelope: RekeyEnvelope) -> WatchCredential
```

Write credentials to Keychain before returning the authenticated
acknowledgement.

- [ ] **Step 5: Verify and commit**

```bash
git add main/apps/app_codex_status/secure_pairing_session.* \
  tests/secure_pairing_session_test.cpp \
  mac-codex-agent/pairing_client.py \
  mac-codex-agent/tests/test_pairing_client.py \
  scripts/run_unified_offline_validation.sh
git commit -m "feat: add Agent pairing sessions"
```

## Task 5: Two-Connection BLE Router And GATT Authorization

**Files:**
- Create: `main/apps/app_codex_status/agent_connection_router.h`
- Create: `main/apps/app_codex_status/agent_connection_router.cpp`
- Create: `tests/agent_connection_router_test.cpp`
- Modify: `main/apps/app_codex_status/codex_ble_transport.h`
- Modify: `main/apps/app_codex_status/codex_ble_transport.cpp`
- Modify: `sdkconfig.defaults`
- Modify: `mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py`
- Create: `mac-codex-agent/tests/test_multi_agent_ble_firmware_contract.py`

- [ ] **Step 1: Write failing router and firmware contract tests**

Assert one Active plus one Pending handle, third-connection rejection,
per-handle MTU/subscription state, production characteristic denial for
Pending, pairing characteristic denial for Active, and no automatic promotion
over an existing Active connection.

- [ ] **Step 2: Run tests and verify RED**

Expected: transport has one global `_connection_handle` and
`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1`.

- [ ] **Step 3: Implement per-handle roles**

Use:

```cpp
enum class AgentConnectionRole : uint8_t { None, Pending, Active };
struct AgentConnection {
    uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
    AgentConnectionRole role = AgentConnectionRole::None;
    AgentFingerprint fingerprint = {};
    uint16_t mtu = 23;
    bool voice_subscribed = false;
};
```

Pass the connection handle from every GATT access callback into the router.
Return ATT authorization errors before queueing unauthorized writes. Notify
only the Active handle. Keep advertising after Active connects so one Pending
central can connect.

- [ ] **Step 4: Configure and gate two connections**

Set `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=2`. Update contract tests to require two,
not an unbounded value. Preserve no-bonding configuration because authentication
is application-layer.

- [ ] **Step 5: Verify and commit**

Run router native tests, BLE firmware contracts, voice transport tests, and a
clean configuration generation.

```bash
git add main/apps/app_codex_status/agent_connection_router.* \
  main/apps/app_codex_status/codex_ble_transport.* \
  tests/agent_connection_router_test.cpp sdkconfig.defaults \
  mac-codex-agent/tests/test_ble_voice_probe_firmware_contract.py \
  mac-codex-agent/tests/test_multi_agent_ble_firmware_contract.py
git commit -m "feat: route active and pending BLE Agents"
```

## Task 6: Watch Agent Panel And Switching

**Files:**
- Create: `main/apps/app_codex_status/agent_panel_model.h`
- Create: `main/apps/app_codex_status/agent_panel_model.cpp`
- Create: `tests/agent_panel_model_test.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Create: `mac-codex-agent/tests/test_agent_panel_firmware_contract.py`

- [ ] **Step 1: Write failing model and layout contract tests**

Test eight rows, Connected/Requesting/Offline states, six-digit request,
duplicate-name suffixes, pull-down ownership, rejection, delete confirmation,
reset confirmation, and switch blocked until voice cleanup completes.

- [ ] **Step 2: Run tests and verify RED**

Expected: no Agent panel model or pull-down panel.

- [ ] **Step 3: Implement the pure panel model**

Expose immutable snapshots and explicit actions:

```cpp
enum class AgentPanelAction {
    None, AllowPairing, AllowAndSwitch, Reject, Delete, ConfirmDelete,
    ResetAll, ConfirmReset
};
```

Model code performs no BLE, NVS, or LVGL calls.

- [ ] **Step 4: Implement the Codex pull-down panel**

Add a vertically revealed LVGL layer above the existing status pages. Preserve
horizontal Codex/Trae paging and global edge controls. Render bounded rows from
the model, require confirmation for Delete/Reset, and dispatch actions outside
the LVGL lock.

- [ ] **Step 5: Integrate safe switching**

On approved switch, call existing bounded `VoiceInputController` cleanup,
wait for its terminal state, disconnect old Active, promote Pending, persist
last selection, then enable status/time/voice for the new Active handle.

- [ ] **Step 6: Verify and commit**

```bash
git add main/apps/app_codex_status/agent_panel_model.* \
  main/apps/app_codex_status/app_codex_status.* \
  tests/agent_panel_model_test.cpp \
  mac-codex-agent/tests/test_agent_panel_firmware_contract.py
git commit -m "feat: add watch Agent switcher"
```

## Task 7: Mac Identity, Per-Watch Credentials, And Rename

**Files:**
- Create: `mac-codex-agent/agent_identity.py`
- Create: `mac-codex-agent/watch_credentials.py`
- Create: `mac-codex-agent/tests/test_agent_identity.py`
- Create: `mac-codex-agent/tests/test_watch_credentials.py`
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/tests/test_app_state.py`

- [ ] **Step 1: Write failing Keychain and naming tests**

Use injected Keychain adapters. Assert identity reuse, private-key
non-exportability from snapshots, normalized names, per-watch isolation,
atomic key/epoch replacement, stale-epoch rejection, and redacted errors.

- [ ] **Step 2: Run tests and verify RED**

Expected: missing identity and credential stores.

- [ ] **Step 3: Implement the stores**

Use Keychain services:

```text
com.stopwatch.codex-agent.identity
com.stopwatch.codex-agent.identity-name
com.stopwatch.codex-agent.watch.{watch_id_hex}
```

Store identity private keys through Security.framework where possible. Store
credential payloads as a versioned binary structure containing exact
`watch_id`, 16-byte key, and nonzero epoch. Never put key bytes in
`Preferences`, `AgentSnapshot`, exceptions, or logs.

- [ ] **Step 4: Add app-state presentation**

Add display-only fields for Agent name, pairing state, verification code, and
watch identity suffix. Keep secret values absent from dataclasses and menu
snapshots.

- [ ] **Step 5: Verify and commit**

```bash
git add mac-codex-agent/agent_identity.py \
  mac-codex-agent/watch_credentials.py \
  mac-codex-agent/tests/test_agent_identity.py \
  mac-codex-agent/tests/test_watch_credentials.py \
  mac-codex-agent/app_state.py mac-codex-agent/tests/test_app_state.py
git commit -m "feat: store Agent identities and watch credentials"
```

## Task 8: Mac Runtime Pairing, Authentication, And Rekey

**Files:**
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`

- [ ] **Step 1: Write failing runtime tests**

Cover unknown watch enrollment, known challenge signing, stale credential
rekey, removed identity denial, Pending isolation, verification timeout,
connection preference preservation, and status continuing while voice is
blocked.

- [ ] **Step 2: Run tests and verify RED**

Expected: runtime loads one fixed Keychain voice key and begins status
immediately after BLE connect.

- [ ] **Step 3: Split transport phases**

Add Pairing GATT UUIDs and typed operations to `BleStatusTransport`. Make
`connect()` return discovered watch identity and authentication state. Do not
start status, time sync, voice notifications, or task writes until
`PairingClient.authenticate()` returns an exact current credential.

- [ ] **Step 4: Replace fixed-key loading**

Remove `load_key_from_keychain` from session startup. Load the per-watch
credential only after discovering `watch_id`; rekey before creating
`VoiceSessionService`; pass the key and epoch into every encoder/decoder.

- [ ] **Step 5: Verify and commit**

Run `test_agent.py`, `test_runtime.py`, pairing client tests, voice session
tests, and connection cleanup tests.

```bash
git add mac-codex-agent/agent.py mac-codex-agent/runtime.py \
  mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py
git commit -m "feat: authenticate and rekey watch connections"
```

## Task 9: Mac Menu Pairing And Agent Naming

**Files:**
- Modify: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/app_state.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`

- [ ] **Step 1: Write failing menu tests**

Assert `Agent Name…`, a bounded rename dialog, pairing request state,
six-digit code display, retry/forget actions, main-thread UI updates, and no
secret values in labels or logs.

- [ ] **Step 2: Run tests and verify RED**

Expected: menu has no identity or pairing controls.

- [ ] **Step 3: Implement menu actions**

Use `NSAlert` plus `NSTextField` for rename, validate through
`AgentIdentityStore`, and queue signed rename synchronization through
`AgentController`. Display the verification code read-only; approval remains
watch-only.

- [ ] **Step 4: Package new modules**

Add all new Python modules to the installer copy list and packaged smoke-test
imports. Extend bundle key-material validation to ensure identity private keys,
watch credentials, and pairing traces are absent.

- [ ] **Step 5: Verify and commit**

```bash
python3 -m unittest \
  mac-codex-agent.tests.test_menu_app \
  mac-codex-agent.tests.test_install_script -v
```

Expected: all tests pass.

```bash
git add mac-codex-agent/menu_app.py mac-codex-agent/app_state.py \
  mac-codex-agent/tests/test_menu_app.py \
  mac-codex-agent/install_launch_agent.sh \
  mac-codex-agent/tests/test_install_script.py
git commit -m "feat: add Agent naming and pairing menu"
```

## Task 10: Remove Build-Time Production Key And Migrate

**Files:**
- Delete: `main/cmake/voice_key_build.cmake`
- Modify: `main/CMakeLists.txt`
- Modify: `scripts/provision_voice_key.py`
- Modify: `mac-codex-agent/tests/test_provision_voice_key.py`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `.gitignore`
- Modify: `README.md`
- Modify: `mac-codex-agent/README.md`

- [ ] **Step 1: Write failing migration contracts**

Assert no production build reads `STOPWATCH_VOICE_KEY_FILE`, no generated
`voice_input_device_key.h` exists, a clean registry initializes at epoch one,
the legacy fixed Keychain item is never used for production, and diagnostic
probe keys remain unchanged.

- [ ] **Step 2: Run tests and verify RED**

Expected: CMake and provisioning tests still require the build-time key.

- [ ] **Step 3: Remove the firmware key build gate**

Delete the CMake module and include, remove generated-header ignores, and make
runtime registry initialization the only production-key source.

- [ ] **Step 4: Convert the provisioning script to legacy cleanup**

Retain an explicit `remove-legacy-voice-key` command for one release. It may
delete only:

```text
service: com.stopwatch.codex-agent.voice
account: StopWatch-Codex
```

It must not generate or import a production key.

- [ ] **Step 5: Update documentation and commit**

Document the one-time firmware migration, six-digit enrollment, eight-Agent
limit, naming, switching, removal/rekey, recovery, and the physical-Flash
security boundary.

```bash
git add -A main/cmake/voice_key_build.cmake main/CMakeLists.txt \
  scripts/provision_voice_key.py \
  mac-codex-agent/tests/test_provision_voice_key.py \
  scripts/run_unified_offline_validation.sh .gitignore \
  README.md mac-codex-agent/README.md
git commit -m "feat: migrate voice keys to secure pairing"
```

## Task 11: Complete Validation And Physical Acceptance

**Files:**
- Modify: `scripts/run_unified_hardware_validation.sh`
- Modify: `docs/superpowers/reports/unified-runtime-validation.md`
- Create: `docs/superpowers/reports/multi-agent-secure-pairing-validation.md`

- [ ] **Step 1: Run all Agent tests in the packaged dependency environment**

```bash
python3 -m venv mac-codex-agent/.venv
mac-codex-agent/.venv/bin/python -m pip install \
  -r mac-codex-agent/requirements.txt
PYTHONPATH=mac-codex-agent \
  mac-codex-agent/.venv/bin/python -m unittest \
  discover -s mac-codex-agent/tests -v
```

Expected: all tests pass with firmware dependencies materialized.

- [ ] **Step 2: Run offline validation and a clean firmware build**

```bash
python3 ./fetch_repos.py
./scripts/run_unified_offline_validation.sh
./scripts/build_unified_candidate.sh --clean
```

Expected: every host/native/contract gate passes; App and assets remain within
their existing limits; internal-memory metrics do not regress past existing
gates.

- [ ] **Step 3: Rebuild and validate the installed Agent**

```bash
./mac-codex-agent/install_launch_agent.sh
codesign --verify --deep --strict \
  "$HOME/Applications/StopWatch Codex Agent.app"
```

Expected: installer and signature validation succeed, with no key material in
the bundle.

- [ ] **Step 4: Run the backup-gated hardware campaign**

With the watch connected and a verified full-Flash backup:

```bash
WATCH_PORTS="$(find /dev -maxdepth 1 -name 'cu.usbmodem*' -print)"
test "$(printf '%s\n' "$WATCH_PORTS" | sed '/^$/d' | wc -l | tr -d ' ')" = "1"
WATCH_PORT="$WATCH_PORTS"
./scripts/run_unified_hardware_validation.sh \
  --port "$WATCH_PORT" \
  --backup backups/watch-full-flash.bin
```

Extend the campaign to record two-Agent enrollment, non-interruption, switching,
rename, reboot, removal, online rekey, offline rekey, old-epoch rejection,
eight-slot saturation, reset-all, Mandarin voice quality, input restoration,
Assistant recovery, and BLE/internal-memory stability.

- [ ] **Step 5: Write evidence and commit**

Record exact commits, build hashes, timestamps, device port, key epochs without
keys, pairing outcomes, test counts, memory metrics, and every manual
observation. Do not record verification codes, public-key packets, ciphertext,
or secret material.

```bash
git add scripts/run_unified_hardware_validation.sh \
  docs/superpowers/reports/unified-runtime-validation.md \
  docs/superpowers/reports/multi-agent-secure-pairing-validation.md
git commit -m "test: validate secure multi-agent pairing"
```
