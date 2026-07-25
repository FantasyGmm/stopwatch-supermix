# StopWatch Voice Production Encryption

## Decision

The user accepted the measured AES-CCM cost on 2026-07-25:

- average watch encryption time: about 1.22 ms per packet;
- p99 watch encryption time: about 1.87 ms per packet;
- estimated active-recording CPU cost: about 6.1 percent of one core;
- wire payload overhead: about 4.7 percent;
- no measured packet loss, sequence error, throughput loss, or visible
  arrival-latency regression.

The strict diagnostic p99 gate of 1 ms did not pass. The user explicitly chose
to continue because the cost exists only while push-to-talk recording is
active.

## Protocol Boundary

Production voice uses protocol version 3. Version 1 remains diagnostic-only.
The watch and installed Agent reject production version 2 so an unencrypted
downgrade cannot be accepted accidentally.

Use AES-128-CCM with an 8-byte authentication tag. The existing clear packet
header is authenticated as AAD. Authentication failure is fail-closed: no
unauthenticated control changes readiness, and no unauthenticated audio reaches
the ADPCM decoder or Core Audio bridge.

The 13-byte CCM nonce is:

```text
domain byte || 8-byte exchange nonce || 32-bit little-endian sequence
```

Use distinct domain bytes for READY, UNAVAILABLE, PTT_BEGIN, AUDIO, PTT_END,
and VOICE_ERROR. A domain value is never reused for another direction or packet
kind.

Use these exact domains:

```text
READY       0x01
UNAVAILABLE 0x02
PTT_BEGIN   0x11
AUDIO       0x12
PTT_END     0x13
VOICE_ERROR 0x14
```

READY and UNAVAILABLE carry a fresh, nonzero, 8-byte Mac-generated exchange
nonce in clear and authenticate their encrypted control body. The watch rejects
a duplicate control nonce on the same BLE connection.

PTT_BEGIN carries a fresh, nonzero, 8-byte watch-generated session nonce in
clear and authenticates the codec, sample rate, and block size. AUDIO, PTT_END,
and VOICE_ERROR derive their nonce from that stored session nonce and their
monotonic per-session sequence. The Mac rejects duplicate recent session
nonces, stale sessions, sequence regression, and authentication failure before
decoding.

PTT_BEGIN uses sequence zero. AUDIO starts at sequence one. PTT_END or
VOICE_ERROR consumes the next sequence. The sequence must never wrap within a
session.

### Control layout

READY and UNAVAILABLE are 24-byte writes:

```text
clear AAD:
  version:u8 = 3
  kind:u8
  reason:u8
  reserved:u8 = 0
  exchange_nonce:8 raw bytes
encrypted body:
  target_bps:u32 little-endian
tag:
  8 bytes
```

READY requires reason zero and target 8192. UNAVAILABLE requires target zero
and a defined nonzero reason. The CCM sequence for both control kinds is zero.

### Notification layout

Every notification starts with the existing 14-byte clear AAD header:

```text
version:u8 = 3
kind:u8
session:u16 little-endian
sequence:u32 little-endian
watch_ms:u32 little-endian
payload_len:u16 little-endian
```

PTT_BEGIN payload is 21 bytes:

```text
session_nonce:8 raw bytes, also appended to the header AAD
encrypted codec:u8, sample_rate:u16, block_samples:u16
tag:8 bytes
```

The only accepted production codec tuple is IMA-ADPCM, 16000 Hz, 320 samples.

AUDIO payload is 172 bytes: one encrypted 164-byte ADPCM block plus the 8-byte
tag. PTT_END payload is the encrypted 26-byte
`<IIIIIIH` summary plus the tag, for 34 bytes. VOICE_ERROR payload is the
encrypted 4-byte `<HH` error body plus the tag, for 12 bytes.

At the validated MTU of 256, an AUDIO characteristic value is:

```text
14-byte clear authenticated header
+ 164-byte encrypted ADPCM block
+ 8-byte CCM tag
= 186 bytes
```

It remains one ATT characteristic value under the 253-byte limit.

### Cross-language vectors

Use this fixed test key and exchange/session nonce only in tests:

```text
key:   000102030405060708090a0b0c0d0e0f
nonce: 1011121314151617
```

The complete READY write for target 8192 is:

```text
030100001011121314151617b0f4d7ed237c66ad647fe0aa
```

The complete PTT_BEGIN notification for session 7, sequence 0, watch time 100,
IMA-ADPCM/16000/320 is:

```text
03010700000000006400000015001011121314151617db7a3c2d9de3ce5c27d655124d
```

Changing any key, domain, nonce, sequence, header/AAD, ciphertext, or tag byte
must fail authentication.

## Key Material

Generate one random 16-byte device key for this personal watch/Mac pair.

The default provisioning file is:

```text
~/Library/Application Support/StopWatch Codex Agent/device-key.hex
```

It contains exactly 32 lowercase hexadecimal characters, is created with mode
0600, and is never copied into the repository or application resources.
`STOPWATCH_VOICE_KEY_FILE` may override the path for isolated tests.

Firmware configuration generates an ignored header inside the build directory
from that file. The generated header is never committed. A firmware build
fails closed on a missing, malformed, or insecurely permissioned key instead
of compiling a public fallback key.

The installer imports the same key into macOS Keychain:

```text
service: com.stopwatch.codex-agent.voice
account: StopWatch-Codex
```

The installed Agent reads the key from Keychain at runtime. The key is not
placed in the app bundle, command-line arguments, logs, reports, or status
snapshots. Tests use explicit temporary keys and never inspect the personal
key.

Losing or rotating the key requires regenerating/importing it, rebuilding and
flashing the watch, and reinstalling the Agent. This protects nearby radio
traffic, but does not claim resistance to physical watch flash extraction
until flash encryption is separately enabled and validated.

## Runtime And Recovery

Encryption initialization and Keychain failure map to a voice-unavailable
state without stopping Codex status or time synchronization.

The Mac Agent subscribes before sending authenticated READY. It sends
authenticated UNAVAILABLE before disabling voice when possible. Disconnect,
quit, malformed packets, tag failures, queue overflow, Typeless failure, and
route failure all execute the existing idempotent cleanup: drain within the
bounded deadline, emit the paired stop Fn tap only if start succeeded, restore
the previous default input, and release watch capture resources.

The watch reuses one keyed mbedTLS CCM context protected against concurrent
access. Key setup is not repeated for every audio packet.

## Acceptance

Automated acceptance requires:

- byte-identical Python/C++ AES-CCM vectors;
- rejection after changing ciphertext, tag, nonce, AAD, version, session, or
  sequence;
- no production version-2 acceptance;
- no key material in tracked files or the generated app bundle;
- exact 186-byte encrypted AUDIO characteristic values at MTU 256;
- zero invalid tags, sequence gaps/regressions, queue overflows, encode
  failures, or sender/receiver plaintext mismatch;
- full offline validation and a clean firmware build;
- physical push-to-talk acceptance with intelligible Mandarin, paired Fn taps,
  previous-input restoration, reconnect cleanup, Assistant recovery, and no
  watch reboot.
