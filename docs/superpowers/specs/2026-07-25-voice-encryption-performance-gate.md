# Voice Encryption Performance Gate

## Decision Rule

The user requires voice encryption when its measured performance impact is
small. If the measured impact is large, implementation must stop and ask
whether encryption is still required.

Existing evidence is not enough to make that decision:

- the 8 KiB/s unencrypted microphone path passes;
- the 16 KiB/s unencrypted margin profile already misses its timing gates;
- no encrypted audio stream or on-watch crypto timing has been measured.

## Candidate

Benchmark application-layer AES-CCM because fresh macOS CoreBluetooth pairing
has repeatedly failed before encrypted characteristic access. BLE link-layer
encryption remains future defense in depth after pairing is reliable.

The candidate uses:

- AES-128-CCM through ESP-IDF mbedTLS with hardware AES enabled;
- a 13-byte nonce derived from benchmark session and packet sequence;
- the existing 14-byte notification header as authenticated additional data;
- the existing 164-byte ADPCM block as plaintext;
- an 8-byte authentication tag;
- a fixed diagnostic key compiled only into the probe and Mac probe client.

The encrypted characteristic value is 186 bytes rather than 178 bytes. It
remains below the 253-byte value limit at the measured ATT MTU of 256. Payload
bandwidth increases by 4.88 percent.

The fixed diagnostic key is not a production key and must never be reused by
the installed Agent. It exists only to isolate cryptographic performance from
enrollment and key-storage work.

## Measurement

Run alternating unencrypted and encrypted profiles on the same firmware,
watch, Mac, and radio environment:

1. three 60-second synthetic 8,192 B/s pairs;
2. three 30-second microphone pairs;
3. one encrypted 16,384 B/s synthetic run as capacity evidence.

Instrument watch-side AES-CCM encryption time per packet and preserve count,
total, maximum, and p99 duration in the probe report. The Mac records tag
failures separately from malformed packets, sequence gaps, and transport
corruption.

## Small-Impact Gate

Encryption is classified as small only when every encrypted 8 KiB/s and
microphone run satisfies:

- zero disconnects, sequence gaps/regressions, malformed packets, invalid tags,
  queue overflows, encode failures, or sender/receiver byte mismatch;
- throughput error no greater than 1 percent;
- p95 arrival deviation no greater than 40 ms;
- maximum arrival gap no greater than 100 ms;
- encrypted p95 no more than 5 ms above its adjacent unencrypted run;
- p99 watch encryption time no greater than 1 ms;
- every encrypted microphone value is exactly 186 bytes at MTU 256;
- WAV duration error no greater than 1 percent;
- intelligible Mandarin without periodic dropout.

The 16 KiB/s result is recorded but does not classify encryption performance,
because its unencrypted baseline already fails timing acceptance.

## Outcome

- If the gate passes, production voice payloads and controls use AES-CCM.
- If the gate fails because encryption increases CPU, loss, queueing, or timing
  beyond these limits, stop and ask the user before adding encryption.
- If failure is unrelated to crypto, fix the underlying transport issue and
  repeat the paired measurement rather than attributing it to encryption.

## Production Key Boundary After A Pass

For this personal project, the local build generates one random device key in
an ignored, mode-0600 secret file. The firmware build consumes that key through
a generated header. The Agent installer imports the same key into macOS
Keychain and does not place it in the public repository or generated app
resources. Losing or rotating the key requires rebuilding/flashing the watch
and reinstalling the Agent.

This protects radio traffic from nearby listeners. It does not claim resistance
to physical watch flash extraction until flash/NVS encryption is separately
enabled and validated.
