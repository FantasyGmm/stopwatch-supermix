# BLE Voice Link Probe

`mac-codex-agent/ble_voice_probe.py` is a standalone diagnostic for measuring
the StopWatch-to-Mac BLE voice data path. It does not invoke Typeless, send the
`Fn` key, or change any production right-button behavior. It is not the voice
input feature itself.

## Security Boundary

This hardware run is a transport and diagnostic crypto capacity probe. Modes 1
and 2 remain transport-only, intentionally unencrypted, and byte-compatible
with the original probe. Mode 3 encrypts only the 164-byte ADPCM microphone block with
AES-128-CCM and an 8-byte tag; its 172-byte payload authenticates the existing
14-byte notification header and produces a 186-byte characteristic value.
The normal status characteristic remains encrypted.

Mode 3 uses a fixed diagnostic key solely to isolate crypto performance from
pairing, enrollment, and key storage. It is not a production key and must
never be used by the installed Agent or production voice path.

Three physical runs already showed that macOS CoreBluetooth could discover and
connect to the custom service, but the encrypted characteristic handshake
timed out with NimBLE status 13. The same result occurred with a fresh
peripheral identity, access-triggered pairing, and volatile non-bonded keys, so
this unencrypted probe must not be treated as production security acceptance.
Production voice input still requires a pairing path that completes on macOS,
or application-layer authentication and encryption.

## Preconditions

1. Fully quit **StopWatch Codex Agent** from its menu-bar menu. Disabling its
   watch-connection toggle is not sufficient: the standalone probe must be the
   only process using the watch BLE service.
2. Open the watch **Codex** app before starting discovery, so it advertises as
   `StopWatch-Codex`.
3. Keep all generated reports, WAV files, and terminal logs outside Git under
   `/tmp/stopwatch-ble-voice-probe/<timestamp>/`.

Create an evidence directory and record the firmware source revision used for
the test:

```bash
ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"
PYTHON="$ROOT/mac-codex-agent/.venv/bin/python"
FIRMWARE="${FIRMWARE:-$ROOT/build-unified/StopWatch-UserDemo.bin}"
EVIDENCE="/tmp/stopwatch-ble-voice-probe/$(date +%Y%m%d-%H%M%S)"
REVISION="$(git -C "$ROOT" rev-parse HEAD)"
test -f "$FIRMWARE"
mkdir -p "$EVIDENCE"
printf '%s\n' "$REVISION" > "$EVIDENCE/firmware-git-revision.txt"
shasum -a 256 "$FIRMWARE" > "$EVIDENCE/firmware-app.sha256"
```

`firmware-git-revision.txt` records the source checkout. `firmware-app.sha256`
records the exact binary flashed for the hardware run; keep both with every
probe report so a rebuilt image is not mistaken for the tested firmware.

The CLI discovers `StopWatch-Codex` by name or its voice-probe BLE service.
Each run requires a duration in seconds and a report destination. Pass the
firmware revision with `--firmware-revision` so the report identifies the
firmware under test.

## Required Synthetic Profiles

Run both required profiles for 60 seconds. Preserve both the JSON report and
the command log even when the command exits nonzero.

```bash
"$PYTHON" mac-codex-agent/ble_voice_probe.py \
  synthetic --target-bps 8192 --duration 60 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/synthetic-8192.json" \
  > "$EVIDENCE/synthetic-8192.log" 2>&1

"$PYTHON" mac-codex-agent/ble_voice_probe.py \
  synthetic --target-bps 16384 --duration 60 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/synthetic-16384.json" \
  > "$EVIDENCE/synthetic-16384.log" 2>&1
```

The 32768 B/s profile is exploratory only. Record its result, but a failure
does not reject BLE after both required profiles pass.

```bash
set +e
"$PYTHON" mac-codex-agent/ble_voice_probe.py \
  synthetic --target-bps 32768 --duration 60 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/synthetic-32768.json" \
  > "$EVIDENCE/synthetic-32768.log" 2>&1
EXPLORATORY_RC=$?
set -e
printf '%s\n' "$EXPLORATORY_RC" > "$EVIDENCE/synthetic-32768.exit-code"
```

## Microphone Profile

Speak normal Mandarin continuously while the 30-second probe runs. The command
creates 16 kHz mono PCM WAV output as well as its JSON report.

```bash
"$PYTHON" mac-codex-agent/ble_voice_probe.py \
  microphone --target-bps 8192 --duration 30 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/microphone.json" \
  --wav "$EVIDENCE/microphone.wav" \
  > "$EVIDENCE/microphone.log" 2>&1
```

Play the saved file and record the human decision separately:

```bash
afplay "$EVIDENCE/microphone.wav"
printf '%s\n' \
  'Manual audio review: intelligible Mandarin; stable speed; no periodic dropout: PASS' \
  > "$EVIDENCE/manual-audio-review.txt"
```

Only record `PASS` after listening confirms intelligible Mandarin, stable
speed, and no periodic dropout. This listening check is a separate manual
acceptance item; an automated transport pass does not establish audio quality.

## Reading The Report

Every JSON report records the requested profile, firmware revision, negotiated
MTU, sender counters, receiver counters, payload bytes, timing metrics, the
automated decision, exception text, and the WAV path when applicable.

For required 8192 and 16384 B/s synthetic profiles, pass requires all of the
following:

- `automated_pass` is `true` and `acceptance_decision` is `pass`.
- Sender generated/accepted packet counts equal `receiver_data_packets`, with
  at least two received data packets.
- `sequence_gaps`, `sequence_regressions`, `payload_corruptions`,
  `queue_overflows`, `encode_failures`, and `invalid_arrival_timing` are zero.
- `sender_receiver_payload_byte_parity` is true, synthetic throughput error is
  at most 1%, `p95_arrival_deviation_ms` is at most 40 ms, and
  `max_arrival_gap_ms` is at most 100 ms.
- `disconnected` is false and `summary_semantically_valid` is true.

For the microphone profile, the same transport conditions apply. In addition,
`sender_receiver_payload_byte_parity` and `microphone_block_byte_parity` must be
true: every generated packet is one 164-byte ADPCM block and sender payload
bytes must equal receiver payload bytes. `wav_duration_error_percent` must also
be at most 1% against `captured_pcm_samples`. The manual listening review
remains required even when these automated checks pass.

## Encrypted Microphone Profile

Run the isolated AES-CCM microphone mode adjacent to its unencrypted baseline:

```bash
"$PYTHON" mac-codex-agent/ble_voice_probe.py \
  microphone-encrypted --target-bps 8192 --duration 30 \
  --firmware-revision "$REVISION" \
  --report "$EVIDENCE/microphone-encrypted.json" \
  --wav "$EVIDENCE/microphone-encrypted.wav" \
  > "$EVIDENCE/microphone-encrypted.log" 2>&1
```

The encrypted report adds `crypto_packets`, `crypto_failures`,
`crypto_total_us`, `crypto_max_us`, `crypto_p99_us`, `invalid_tags`, and
`encrypted_characteristic_value_bytes`. A run fails closed if authentication
fails: the invalid block never reaches ADPCM decoding. Automated acceptance
also requires zero crypto failures and invalid tags, p99 watch encryption time
at most 1 ms, and every encrypted characteristic value to be exactly 186
bytes. The same WAV duration and manual Mandarin listening checks still apply.

The wire protocol currently defines only encrypted microphone mode 3. It does
not define an encrypted synthetic source mode; do not label a mode 3 microphone
run as encrypted synthetic evidence.

The 32768 B/s result is labeled `exploratory_pass` or `exploratory_fail`; it is
recorded for capacity information and is not a required BLE acceptance gate.

## Cleanup And Recovery

After every probe, close the watch Codex app. Open **Assistant** and verify it
can acquire the microphone. Then reopen Codex, manually launch the normal
**StopWatch Codex Agent**, and verify that status BLE reconnects. Fully quit the
Agent again before running any subsequent standalone probe; leaving it running
will create two processes competing for the same watch BLE service. Keep
reports, WAV files, and logs under the evidence directory for diagnosis.

An offline build or a successful firmware flash only proves that the image was
built or written. It does not prove BLE transport, microphone ownership,
reconnect behavior, or audio quality on hardware; those require the physical
probe and recovery checks above.
