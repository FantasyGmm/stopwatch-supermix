# Codex Custom Pet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Allow the macOS Agent to convert one GIF, preview the exact watch-native result, and atomically install it as the persistent looping pet on the watch Codex page.

**Architecture:** The Agent converts bounded GIF input into a deterministic `CXP1` indexed/RLE package, then transfers it over a new authenticated optional GATT service. The watch validates into an inactive storage slot, atomically selects it with one CRC-protected NVS record, and renders it through a UI-owned PSRAM decoder with the firmware robot as unconditional fallback.

**Tech Stack:** Python 3.9+, Pillow 11, PyObjC/AppKit, Bleak, cryptography AES-CCM, C++17, ESP-IDF 5.5.4, NimBLE, FATFS wear levelling, NVS, mbedTLS SHA-256/AES-CCM, LVGL 9.

**Spec:** `docs/superpowers/specs/2026-08-22-codex-custom-pet-design.md`

## Global Constraints

- Accept GIF87a/GIF89a only, at most 10 MiB, `1024 x 1024`, 240 source frames, and 64 million cumulative decoded pixels.
- Produce exactly `144 x 156` non-antialiased transparent canvases, at most 24 frames, `60..1000 ms` per output frame, infinite loop, and a package no larger than 256 KiB.
- Keep UUID suffixes `0006` through `000a` unchanged; allocate service/control/status with suffixes `000b`, `000c`, and `000d`.
- Permit the channel only for the active authenticated Production connection and retire transfers on disconnect, Agent switch, credential epoch change, timeout, cancellation, or commit.
- Store under `/spiflash/codex_pet`, retain at least 128 KiB free before Begin, use two slots plus one CRC-protected NVS selection blob, and never render staging bytes.
- Preserve the built-in robot as unconditional fallback and preserve custom storage across ordinary firmware flashing.
- Keep Codex logo, status row, page dots at `y=416`, and battery at `y=432`; use pet box `x=55, y=88, w=144, h=156`.
- Treat the pet GATT channel as optional: an older firmware must yield `Firmware update required` without disrupting status, voice, input actions, pairing, or reconnect.
- Do not package source GIFs, image payload logs, credentials, keys, or nonces in Agent logs or the installed app.
- Use focused red/green tests during implementation; use a clean firmware build, installed-Agent test, live upload, reboot, normal flash, and physical AMOLED comparison only at the release gate.

## File Structure

- `mac-codex-agent/pet_package.py`: canonical `CXP1` constants, models, PackBits codec, serializer, parser, and preview-frame reconstruction.
- `mac-codex-agent/pet_converter.py`: bounded Pillow GIF inspection, frame coalescing, nearest-neighbor fitting, delay normalization, sampling, quantization, and conversion metadata.
- `mac-codex-agent/pet_protocol.py`: authenticated transfer message encoding/decoding, AES-CCM domains, replay checks, status/error models, and fixed wire vectors.
- `mac-codex-agent/pet_upload.py`: asynchronous query/upload/cancel/restore state machine over transport callbacks.
- `mac-codex-agent/pet_cache.py`: watch-identity/package-hash preview cache with atomic local publication.
- `mac-codex-agent/pet_window.py`: independent AppKit window, exact-package preview, picker/drop target, actions, progress, and unsupported/offline/error states.
- `main/apps/app_codex_status/codex_pet_package.{h,cpp}`: allocation-safe `CXP1` view validation, PackBits decode, digest verification, and RGB565A8 output.
- `main/apps/app_codex_status/codex_pet_protocol.{h,cpp}`: bounded command/status wire format and replay state.
- `main/apps/app_codex_status/codex_pet_store.{h,cpp}`: two FAT slots, capacity gate, NVS selection record, boot recovery, restore, and generation publication.
- `main/apps/app_codex_status/codex_pet_transfer.{h,cpp}`: authenticated transaction state machine joining transport, protocol, and store without owning LVGL objects.
- `main/apps/app_codex_status/codex_pet_runtime.{h,cpp}`: PSRAM package/frame ownership, LVGL descriptor publication, timing, pause/resume, generation swap, and fallback.
- Existing `codex_ble_transport`, `app_codex_status`, `runtime.py`, `agent.py`, `menu_app.py`, installer, validation runner, and focused tests are modified only at their integration seams.

---

### Task 1: Canonical Python CXP1 Package Codec

**Files:**
- Create: `mac-codex-agent/pet_package.py`
- Create: `mac-codex-agent/tests/test_pet_package.py`
- Create: `tests/fixtures/codex_pet/cxp1_minimal.hex`

**Interfaces:**
- Produces: `PetFrame(duration_ms: int, indices: bytes)`, `PetPackage(width: int, height: int, palette_rgb565: tuple[int, ...], transparent_index: int, frames: tuple[PetFrame, ...], encoded: bytes, sha256: bytes)`, `encode_package(width: int, height: int, palette_rgb565: tuple[int, ...], transparent_index: int, frames: tuple[PetFrame, ...]) -> PetPackage`, `parse_package(data: bytes) -> PetPackage`, `render_rgba_frames(package: PetPackage) -> tuple[bytes, ...]`, `packbits_encode(data: bytes) -> bytes`, and `packbits_decode(data: bytes, expected_size: int) -> bytes`.
- Canonical header: 72 bytes, little-endian `<4s8H5I32s`; fields are magic, schema, header size, width, height, frame count, opaque palette count, transparent index, flags, frame-table offset, palette offset, payload offset, total length, decoded-pixel count, digest.
- Frame entry: 12 bytes, little-endian `<HHII`; fields are duration, reserved zero, payload offset, payload length.
- Digest input: the entire package with the 32-byte digest field replaced by zero bytes.

- [ ] **Step 1: Write failing package and PackBits tests**

```python
def test_cxp1_round_trip_and_fixed_header(self):
    pixels = 144 * 156
    frames = (
        PetFrame(180, bytes([0]) * pixels),
        PetFrame(240, bytes([1]) * pixels),
    )
    package = encode_package(144, 156, (0xFFFF,), 1, frames)
    self.assertEqual(package.encoded[:4], b"CXP1")
    self.assertEqual(len(package.sha256), 32)
    self.assertEqual(parse_package(package.encoded).frames, frames)

def test_packbits_rejects_overrun_and_trailing_output(self):
    with self.assertRaises(PetPackageError):
        packbits_decode(bytes([0xFE, 0x04]), expected_size=2)
```

Also assert exact 72-byte header size; frame count `1..24`; width 144 and height 156 for production packages; palette count `1..255`; transparent index equal to palette count; delays `60..1000`; zero reserved fields; total length at most 262144; decoded pixel count equal to `width * height * frame_count`; strictly increasing non-overlapping payload spans; exact RLE output size; palette/index bounds; and digest mismatch rejection.

- [ ] **Step 2: Run the focused test and confirm the missing module failure**

Run: `python3 -m unittest mac-codex-agent/tests/test_pet_package.py -v`

Expected: FAIL with `ModuleNotFoundError: No module named 'pet_package'`.

- [ ] **Step 3: Implement the serializer, parser, and deterministic PackBits codec**

```python
MAGIC = b"CXP1"
SCHEMA_VERSION = 1
WIDTH = 144
HEIGHT = 156
MAX_FRAMES = 24
MAX_PACKAGE_BYTES = 256 * 1024
HEADER = struct.Struct("<4s8H5I32s")
FRAME_ENTRY = struct.Struct("<HHII")

@dataclasses.dataclass(frozen=True)
class PetFrame:
    duration_ms: int
    indices: bytes

@dataclasses.dataclass(frozen=True)
class PetPackage:
    width: int
    height: int
    palette_rgb565: tuple[int, ...]
    transparent_index: int
    frames: tuple[PetFrame, ...]
    encoded: bytes
    sha256: bytes
```

Encode PackBits with literal runs of 1–128 bytes and repeated runs of 3–130 bytes; prefer a repeated run at length 3 and above so identical input always yields identical bytes. Perform every integer/range/product/offset check before slicing or allocating decoded output. `render_rgba_frames` expands RGB565 back to deterministic 8-bit RGB and uses alpha zero only for the reserved transparent index, making it the sole preview source. Generate `cxp1_minimal.hex` from the two-frame test package and assert its complete lowercase hex text in the Python test so it becomes the cross-language fixture.

- [ ] **Step 4: Run red/green verification**

Run: `python3 -m unittest mac-codex-agent/tests/test_pet_package.py -v`

Expected: all package tests PASS, including one-bit digest corruption and malformed offset cases.

- [ ] **Step 5: Commit the package codec**

```bash
git add mac-codex-agent/pet_package.py mac-codex-agent/tests/test_pet_package.py tests/fixtures/codex_pet/cxp1_minimal.hex
git commit -m "feat: define Codex pet package format"
```

### Task 2: Bounded GIF Conversion and Exact Preview Frames

**Files:**
- Create: `mac-codex-agent/pet_converter.py`
- Create: `mac-codex-agent/tests/test_pet_converter.py`

**Interfaces:**
- Consumes: `pet_package.encode_package` and `PetPackage` from Task 1.
- Produces: `ConversionMetadata(source_frames, output_frames, loop_duration_ms, width, height, package_bytes)`, `ConvertedPet(package: PetPackage, preview_rgba: tuple[bytes, ...], metadata: ConversionMetadata)`, `convert_gif(path: pathlib.Path) -> ConvertedPet`, and `PetConversionError(code: str, detail: str)`.

- [ ] **Step 1: Write generated-fixture tests for safety and frame semantics**

```python
def test_converts_disposal_transparency_to_exact_watch_canvas(self):
    path = self.make_gif(
        canvas=(4, 4),
        durations=(40, 180, 1200),
        disposal=(1, 2, 3),
        transparent=True,
    )
    converted = convert_gif(path)
    self.assertEqual((converted.metadata.width, converted.metadata.height), (144, 156))
    self.assertTrue(all(len(frame) == 144 * 156 * 4 for frame in converted.preview_rgba))
    self.assertEqual([frame.duration_ms for frame in converted.package.frames], [60, 180, 1000])

def test_rejects_cumulative_pixel_bomb_before_frame_materialization(self):
    path = self.make_header_only_gif(canvas=(1024, 1024), declared_frames=61)
    with self.assertRaisesRegex(PetConversionError, "decoded pixel budget"):
        convert_gif(path)
```

Generate fixtures in each test with Pillow and temporary directories. Cover GIF87a/GIF89a signatures, truncated input, 10 MiB boundary, 1024-square boundary, 240-frame boundary, 64-million-pixel boundary, disposal 0/1/2/3, local palettes, partial frames, transparent pixels, duplicate-frame delay accumulation, more than 24 frames, aspect fitting, nearest-neighbor scaling, and package-size rejection.

- [ ] **Step 2: Run the converter test and observe the missing converter failure**

Run: `python3 -m unittest mac-codex-agent/tests/test_pet_converter.py -v`

Expected: FAIL with `ModuleNotFoundError: No module named 'pet_converter'`.

- [ ] **Step 3: Implement bounded inspection, coalescing, sampling, and quantization**

```python
MAX_INPUT_BYTES = 10 * 1024 * 1024
MAX_CANVAS = (1024, 1024)
MAX_SOURCE_FRAMES = 240
MAX_DECODED_PIXELS = 64_000_000
MAX_OUTPUT_FRAMES = 24
MIN_DELAY_MS = 60
MAX_DELAY_MS = 1000

def convert_gif(path: pathlib.Path) -> ConvertedPet:
    source = _read_bounded(path, MAX_INPUT_BYTES)
    inspection = _inspect_gif_blocks(source)
    _validate_inspection(inspection)
    rgba_frames = _coalesce_with_pillow(source, inspection)
    timed = _merge_identical_frames(rgba_frames)
    sampled = _sample_timeline(timed, MAX_OUTPUT_FRAMES)
    canvases = tuple(_fit_nearest(frame, 144, 156) for frame in sampled)
    palette, transparent_index, indexed = _quantize_global(canvases, 255)
    package = encode_package(144, 156, palette, transparent_index, indexed)
    return _make_result(package, render_rgba_frames(package))
```

`_inspect_gif_blocks` walks the GIF block stream without allocating canvases, counts image descriptors, validates sub-block termination, and computes logical-canvas pixels multiplied by frame count. `_coalesce_with_pillow` loads only after this check and materializes complete RGBA frames using disposal extents. `_sample_timeline` selects at most 24 evenly spaced timestamps, merges repeated selections, and redistributes durations so the clamped total is closest to the source loop duration. `_quantize_global` reserves one transparent index and maps all pixels with alpha below 128 to it; all others use one deterministic no-dither adaptive palette converted to RGB565.

- [ ] **Step 4: Verify converter behavior and the supplied GIF**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_pet_converter.py -v
PYTHONPATH=mac-codex-agent python3 -c 'from pathlib import Path; from pet_converter import convert_gif; p=convert_gif(Path("/var/folders/39/nvts3ng12vv7cf8yx9460krh0000gn/T/codex-clipboard-18ecce5e-fd00-4e92-a4f1-373567bffc74.gif")); print(p.metadata, p.package.sha256.hex())'
```

Expected: tests PASS; supplied fixture reports 6 source frames, 6 output frames, 1080 ms loop duration, 144×156 output, and a package no larger than 262144 bytes.

- [ ] **Step 5: Commit conversion**

```bash
git add mac-codex-agent/pet_converter.py mac-codex-agent/tests/test_pet_converter.py
git commit -m "feat: convert bounded GIFs into Codex pets"
```

### Task 3: Mirrored C++ Package Parser and Frame Decoder

**Files:**
- Create: `main/apps/app_codex_status/codex_pet_package.h`
- Create: `main/apps/app_codex_status/codex_pet_package.cpp`
- Create: `tests/codex_pet_package_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

**Interfaces:**
- Consumes: Task 1's exact header, frame entry, digest, palette, and PackBits rules.
- Produces: C++17-compatible `codex_pet::ByteView`, `codex_pet::MutableByteView`, `codex_pet::PackageView`, `codex_pet::ParseResult parse_package(ByteView)`, `bool verify_digest(ByteView)`, and `DecodeResult decode_frame(const PackageView&, uint16_t, MutableByteView rgb565a8)`.
- Output buffer layout: `144 * 156 * 2` little-endian RGB565 bytes followed by `144 * 156` alpha bytes; transparent pixels get RGB565 zero and alpha zero, opaque pixels get palette color and alpha 255.

- [ ] **Step 1: Add a host test that consumes the shared hex vector**

```cpp
int main()
{
    const auto bytes = read_hex("tests/fixtures/codex_pet/cxp1_minimal.hex");
    const auto parsed = codex_pet::parse_package({bytes.data(), bytes.size()});
    assert(parsed.error == codex_pet::PackageError::None);
    assert(parsed.package.width == 144);
    assert(parsed.package.height == 156);
    assert(parsed.package.frames.size() == 2);
    std::array<uint8_t, 144 * 156 * 3> output{};
    assert(codex_pet::decode_frame(parsed.package, 0, {output.data(), output.size()}) == codex_pet::DecodeError::None);
}
```

Add mutations for every multi-byte field, overflowed products, overlapping payloads, nonzero reserved fields, invalid palette indices, RLE overrun/underrun, trailing compressed bytes, and digest mismatch.

- [ ] **Step 2: Register and run the new host stage to prove it is red**

Add a `Codex pet package host test` stage to the unified runner, compiling the test and package implementation with the runner's existing static mbedcrypto build. Add the exact stage and source list to `test_unified_voice_validation.py`.

Run: `IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only`

Expected: FAIL because `codex_pet_package.h` or its functions do not exist.

- [ ] **Step 3: Implement allocation-safe parsing, SHA-256, and frame decode**

```cpp
namespace codex_pet {
constexpr size_t kWidth = 144;
constexpr size_t kHeight = 156;
constexpr size_t kPixelsPerFrame = kWidth * kHeight;
constexpr size_t kRgb565A8Bytes = kPixelsPerFrame * 3;
constexpr size_t kMaxPackageBytes = 256 * 1024;

struct ByteView { const uint8_t* data; size_t size; };
struct MutableByteView { uint8_t* data; size_t size; };

ParseResult parse_package(ByteView bytes);
bool verify_digest(ByteView bytes);
DecodeError decode_frame(
    const PackageView& package,
    uint16_t frame_index,
    MutableByteView rgb565a8);
}
```

Read integers bytewise instead of casting packed structs. Validate additions with `offset <= size && length <= size - offset`, and products with division checks before constructing spans. Hash the package in three mbedTLS updates: bytes before digest, 32 zero bytes, bytes after digest. Decode RLE directly to bounded palette indices, then fill the color plane and alpha plane without an intermediate full-frame allocation.

- [ ] **Step 4: Run the Python and C++ fixed-vector tests**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_pet_package.py mac-codex-agent/tests/test_unified_voice_validation.py -v
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: package/vector tests and the exact new host stage PASS.

- [ ] **Step 5: Commit the mirrored decoder**

```bash
git add main/apps/app_codex_status/codex_pet_package.h main/apps/app_codex_status/codex_pet_package.cpp tests/codex_pet_package_test.cpp scripts/run_unified_offline_validation.sh mac-codex-agent/tests/test_unified_voice_validation.py
git commit -m "feat: decode Codex pet packages on watch"
```

### Task 4: Authenticated Pet Transfer Protocol

**Files:**
- Create: `mac-codex-agent/pet_protocol.py`
- Create: `mac-codex-agent/tests/test_pet_protocol.py`
- Create: `main/apps/app_codex_status/codex_pet_protocol.h`
- Create: `main/apps/app_codex_status/codex_pet_protocol.cpp`
- Create: `tests/codex_pet_protocol_test.cpp`
- Modify: `mac-codex-agent/voice_input_crypto.py`
- Modify: `main/apps/app_codex_status/voice_input_crypto.h`
- Modify: `main/apps/app_codex_status/voice_input_crypto.cpp`
- Modify: `tests/voice_input_crypto_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

**Interfaces:**
- Produces message kinds `Query=0x01`, `Begin=0x02`, `Chunk=0x03`, `Commit=0x04`, `Cancel=0x05`, `Restore=0x06`; status kinds `State=0x81`, `Progress=0x82`, `Committed=0x83`, `Cancelled=0x84`, `Error=0xff`.
- Adds AES-CCM domains `PetControl=0x30` and `PetStatus=0x31` to the existing Production key context; nonce is `domain || exchange_nonce[8] || sequence_le32`, with exact monotonic sequence bounds `1..0xffffffff`.
- Produces Python `PetStatus`, `encode_control(key: bytes, nonce: bytes, sequence: int, control: PetControl) -> bytes`, `decode_control(key: bytes, frame: bytes) -> PetControl`, `encode_status(key: bytes, nonce: bytes, sequence: int, status: PetStatus) -> bytes`, `decode_status(key: bytes, frame: bytes) -> PetStatus`, and C++ mirrored `pet_protocol::Control`, `pet_protocol::Status`, `ControlReplayGuard`, and encode/decode functions.

- [ ] **Step 1: Write mirrored wire and replay tests**

```python
def test_chunk_round_trip_and_tamper_rejection(self):
    frame = encode_chunk(KEY, NONCE, sequence=7, transaction_id=9, offset=20, data=b"abc")
    self.assertEqual(decode_control(KEY, frame).chunk.data, b"abc")
    changed = frame[:-1] + bytes([frame[-1] ^ 1])
    with self.assertRaises(PetProtocolError):
        decode_control(KEY, changed)
```

The Python and C++ tests must share exact hex vectors for Query, Begin, Chunk, Commit, Cancel, Restore, State, Progress, Committed, and Error. Test zero nonce, wrong domain/key/tag, boolean-as-integer Python arguments, zero/wrapped sequences, stale sequence, exact duplicate `(sequence, offset, ciphertext)` acceptance, conflicting duplicate rejection, out-of-order offset rejection, oversized payload, malformed status fields, and unsupported schema reporting.

- [ ] **Step 2: Run both protocol suites red**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_pet_protocol.py -v
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: FAIL because pet protocol modules and crypto domains do not exist.

- [ ] **Step 3: Implement exact frames and domain separation**

```python
class MessageKind(enum.IntEnum):
    QUERY = 0x01
    BEGIN = 0x02
    CHUNK = 0x03
    COMMIT = 0x04
    CANCEL = 0x05
    RESTORE = 0x06

PET_CONTROL_DOMAIN = 0x30
PET_STATUS_DOMAIN = 0x31
MAX_CHUNK_BYTES = 480
```

Use a common clear prefix `kind:u8, version:u8, sequence:u32, exchange_nonce:8`; authenticate that prefix as AAD and encrypt the kind-specific body with an 8-byte CCM tag. `Begin` body contains transaction ID, package length, frame count, dimensions, schema, and SHA-256. `Chunk` contains transaction ID, offset, length, and bytes. `State` contains source, schema min/max, generation, usable flag, and active hash. `Error` contains a bounded numeric reason plus required/available sizes where relevant; user-facing text remains on the Mac.

Lock every body to these little-endian fields:

- Query: `transaction_id:u32`.
- Begin: `transaction_id:u32, schema:u16, width:u16, height:u16, frame_count:u16, package_length:u32, sha256:32`.
- Chunk: `transaction_id:u32, offset:u32, data_length:u16, data:data_length`.
- Commit/Cancel/Restore: `transaction_id:u32`.
- State: `transaction_id:u32, source:u8, usable:u8, schema_min:u16, schema_max:u16, reserved:u16=0, generation:u32, active_sha256:32`; built-in uses an all-zero hash.
- Progress: `transaction_id:u32, acknowledged_bytes:u32, total_bytes:u32`.
- Committed: `transaction_id:u32, generation:u32, active_sha256:32`.
- Cancelled: `transaction_id:u32`.
- Error: `transaction_id:u32, reason:u16, reserved:u16=0, required_bytes:u32, available_bytes:u32`.

Every Agent-created operation starts with a fresh nonzero exchange nonce and sequence one. Status frames echo that exchange nonce and use their own strictly increasing status sequence. Retire both replay guards when the operation ends; retain a bounded completed-transaction tombstone until the connection generation changes so late Commit/Progress frames cannot be accepted by a new operation.

- [ ] **Step 4: Run mirrored vectors and existing voice crypto regressions**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_pet_protocol.py mac-codex-agent/tests/test_voice_input_crypto.py -v
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: pet vectors match byte-for-byte and existing voice/input crypto tests remain PASS.

- [ ] **Step 5: Commit the protocol**

```bash
git add mac-codex-agent/pet_protocol.py mac-codex-agent/tests/test_pet_protocol.py mac-codex-agent/voice_input_crypto.py main/apps/app_codex_status/codex_pet_protocol.h main/apps/app_codex_status/codex_pet_protocol.cpp main/apps/app_codex_status/voice_input_crypto.h main/apps/app_codex_status/voice_input_crypto.cpp tests/codex_pet_protocol_test.cpp tests/voice_input_crypto_test.cpp scripts/run_unified_offline_validation.sh mac-codex-agent/tests/test_unified_voice_validation.py
git commit -m "feat: authenticate Codex pet transfers"
```

### Task 5: Atomic Two-Slot Watch Storage

**Files:**
- Create: `main/apps/app_codex_status/codex_pet_store.h`
- Create: `main/apps/app_codex_status/codex_pet_store.cpp`
- Create: `tests/codex_pet_store_test.cpp`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

**Interfaces:**
- Consumes: `codex_pet::parse_package` and `verify_digest` from Task 3.
- Produces: `PetSelection { Source source; uint32_t generation; uint16_t schema; std::array<uint8_t,32> hash; uint8_t slot; bool usable; }`, `begin(length, metadata)`, `write(offset, bytes)`, `commit()`, `cancel()`, `restore_default()`, `load_selection()`, and immutable `generation()`.
- Production paths: `/spiflash/codex_pet/slot0.cxp` and `/spiflash/codex_pet/slot1.cxp`; NVS namespace `cxpet`, key `active`.

- [ ] **Step 1: Write a fault-injected storage model test**

```cpp
for(const auto point : all_commit_fault_points()) {
    FakePetStorage io;
    install_valid_generation(io, 4, 0, old_package);
    io.fail_at(point);
    CodexPetStore store(io);
    attempt_install(store, new_package);
    const auto after_boot = CodexPetStore(io).load_selection();
    assert(after_boot.generation == 4 || after_boot.generation == 5);
    assert(after_boot.hash == old_hash || after_boot.hash == new_hash);
    assert(!after_boot.references_partial_file);
}
```

Cover directory creation, free-space query, inactive-slot truncate/open, every chunk write, flush, fsync, close, complete parse, digest verification, NVS blob write/commit, acknowledgement boundary, old-slot cleanup, both-slot scan, CRC corruption, unsupported schema, truncated files, restore-default marker commit, cleanup warning, generation wrap rejection, and 128 KiB reserve calculations.

- [ ] **Step 2: Add the host stage and confirm the storage test fails**

Run: `IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only`

Expected: FAIL because `CodexPetStore` and its injected filesystem/NVS ports are missing.

- [ ] **Step 3: Implement storage behind narrow injectable ports**

```cpp
class PetFileSystem {
public:
    virtual ~PetFileSystem() = default;
    virtual uint64_t free_bytes() const = 0;
    virtual bool write_at(uint8_t slot, uint32_t offset, codex_pet::ByteView bytes) = 0;
    virtual bool flush_close(uint8_t slot) = 0;
    virtual bool read_exact(uint8_t slot, codex_pet::MutableByteView out) const = 0;
    virtual std::optional<size_t> file_size(uint8_t slot) const = 0;
    virtual bool remove(uint8_t slot) = 0;
};

class PetBufferAllocator {
public:
    virtual ~PetBufferAllocator() = default;
    virtual uint8_t* allocate(size_t bytes) = 0;
    virtual void release(uint8_t* buffer) = 0;
};

class PetSelectionStore {
public:
    virtual ~PetSelectionStore() = default;
    virtual bool load(SelectionRecord& record) const = 0;
    virtual bool commit(const SelectionRecord& record) = 0;
};
```

Keep the selection record a single versioned fixed-size blob containing magic, record version, source, slot, schema, generation, 32-byte hash, and CRC32. Before `begin`, remove only the inactive stale slot, then check `free_bytes >= proposed_length + 131072` using subtraction-safe arithmetic; never remove the active slot for capacity. `write` permits an exact already-written duplicate only after comparing bytes; any conflicting or gap offset retires staging. Boot prefers a valid selected slot, otherwise scans both valid packages for the highest non-wrapped generation, otherwise returns built-in. Production adapters use `statvfs`, bounded file I/O, `fsync`, one NVS blob commit, and a `heap_caps_malloc(package_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` allocator for package validation so a 256 KiB file is never copied to a large task-stack or ordinary internal-RAM vector.

- [ ] **Step 4: Run package plus storage host tests**

Run: `IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only`

Expected: every injected boundary selects old complete, new complete, or built-in; never staging.

- [ ] **Step 5: Commit storage**

```bash
git add main/apps/app_codex_status/codex_pet_store.h main/apps/app_codex_status/codex_pet_store.cpp tests/codex_pet_store_test.cpp scripts/run_unified_offline_validation.sh mac-codex-agent/tests/test_unified_voice_validation.py
git commit -m "feat: persist Codex pets atomically"
```

### Task 6: Watch Transfer Controller and Optional GATT Service

**Files:**
- Create: `main/apps/app_codex_status/codex_pet_transfer.h`
- Create: `main/apps/app_codex_status/codex_pet_transfer.cpp`
- Create: `tests/codex_pet_transfer_test.cpp`
- Modify: `main/apps/app_codex_status/codex_ble_transport.h`
- Modify: `main/apps/app_codex_status/codex_ble_transport.cpp`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `tests/codex_runtime_session_test.cpp`
- Modify: `tests/agent_connection_router_test.cpp`
- Modify: `mac-codex-agent/tests/test_multi_agent_ble_firmware_contract.py`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

**Interfaces:**
- Consumes: Task 4 controls/status and Task 5 store.
- Produces: `CodexPetControlFrame`, `take_pet_control`, `pet_subscribed`, `notify_pet_status`, `set_pet_credential`, `clear_pet_credential`, and `CodexPetTransfer::handle(control, now_ms, active_generation, key_epoch)`.
- Queue bound: eight inbound control writes; process at most four controls or 2 ms of work per app loop, whichever occurs first.

- [ ] **Step 1: Write controller lifecycle and GATT contract tests**

```cpp
assert(!controller.handle(begin, now, pending_connection, epoch).accepted);
assert(controller.handle(begin, now, active_connection, epoch).accepted);
assert(controller.handle(chunk0, now + 1, active_connection, epoch).progress == chunk0.data.size());
controller.on_disconnect(active_connection);
assert(!controller.transaction_active());
assert(store.selection().hash == old_hash);
```

Test unauthorized/pending access, absent notification subscription, stale connection generation, fresh nonce, replay, exact duplicate, conflicting duplicate, queue overflow, timeout, disconnect, credential replacement, Agent switch, Cancel, Restore, commit exactly once, late callbacks, and cleanup exceptions. Contract-test the three complete UUID literals and confirm all existing UUIDs remain unchanged.

- [ ] **Step 2: Register tests and observe the missing controller/service failure**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_multi_agent_ble_firmware_contract.py -v
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: FAIL for missing UUID suffixes `000b/000c/000d` and controller types.

- [ ] **Step 3: Add the optional service without coupling existing channels**

```cpp
constexpr size_t kPetControlQueueDepth = 8;
struct CodexPetControlFrame {
    uint16_t connection_handle = secure_pairing::kNoAgentConnection;
    uint16_t length = 0;
    std::array<uint8_t, 512> data{};
};

bool CodexBleTransport::take_pet_control(CodexPetControlFrame& frame);
CodexBleNotifyResult CodexBleTransport::notify_pet_status(
    const uint8_t* data, size_t length);
```

Register a separate primary service `7a23000b-5474-4f50-5741-544348434f44`; write characteristic `7a23000c-5474-4f50-5741-544348434f44`; notify characteristic `7a23000d-5474-4f50-5741-544348434f44`. Authorize both through `AgentGattSurface::Production`. Track its subscription and queue independently; pet setup/cleanup errors must not clear voice or input subscriptions. Drain bounded controls in the existing app loop and publish only immutable selection generation/hash to UI state.

- [ ] **Step 4: Run protocol, router, transfer, and lifecycle regressions**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_multi_agent_ble_firmware_contract.py -v
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: new tests PASS; existing status, pairing, voice, input-action, and multi-Agent tests remain PASS.

- [ ] **Step 5: Commit watch transport**

```bash
git add main/apps/app_codex_status/codex_pet_transfer.h main/apps/app_codex_status/codex_pet_transfer.cpp main/apps/app_codex_status/codex_ble_transport.h main/apps/app_codex_status/codex_ble_transport.cpp main/apps/app_codex_status/app_codex_status.h main/apps/app_codex_status/app_codex_status.cpp tests/codex_pet_transfer_test.cpp tests/codex_runtime_session_test.cpp tests/agent_connection_router_test.cpp mac-codex-agent/tests/test_multi_agent_ble_firmware_contract.py scripts/run_unified_offline_validation.sh mac-codex-agent/tests/test_unified_voice_validation.py
git commit -m "feat: receive authenticated Codex pet uploads"
```

### Task 7: Watch Pet Runtime, Live Swap, and Approved Layout

**Files:**
- Create: `main/apps/app_codex_status/codex_pet_runtime.h`
- Create: `main/apps/app_codex_status/codex_pet_runtime.cpp`
- Create: `tests/codex_pet_runtime_test.cpp`
- Modify: `main/apps/app_codex_status/codex_status_ui_layout.h`
- Modify: `main/apps/app_codex_status/app_codex_status.h`
- Modify: `main/apps/app_codex_status/app_codex_status.cpp`
- Modify: `tests/codex_status_ui_layout_test.cpp`
- Modify: `mac-codex-agent/tests/test_codex_status_f_layout_contract.py`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

**Interfaces:**
- Consumes: Task 3 decoder and Task 5 immutable selection generation/hash.
- Produces: `CodexPetRuntime::open(selection)`, `prepare_generation(selection)`, `advance(now_ms)`, `set_visible(bool)`, `current_image() -> const lv_image_dsc_t*`, `take_frame_changed()`, `using_custom()`, and `close()`.

- [ ] **Step 1: Lock runtime behavior and all approved coordinates in failing tests**

```cpp
static_assert(codex_status_ui::kPetX == 55);
static_assert(codex_status_ui::kPetY == 88);
static_assert(codex_status_ui::kPetWidth == 144);
static_assert(codex_status_ui::kPetHeight == 156);
static_assert(codex_status_ui::kPetScale == 384);
static_assert(codex_status_ui::kBeaconSpine.x == 210);
static_assert(codex_status_ui::kBeaconSpine.y == 96);
static_assert(codex_status_ui::kBeaconSpine.h == 120);
static_assert(codex_status_ui::kTelemetryX == 231);
static_assert(codex_status_ui::kWeekY == 246);
static_assert(codex_status_ui::kAlertDividerY == 282);
static_assert(codex_status_ui::kStateLabelY == 294);
static_assert(codex_status_ui::kStateCountY == 320);
static_assert(codex_status_ui::kTaskIndexY == 306);
static_assert(codex_status_ui::kAlertProjectY == 332);
static_assert(codex_status_ui::kAlertDetailY == 363);
static_assert(codex_status_ui::kPageIndicatorY == 416);
static_assert(codex_status_ui::kBatteryY == 432);
```

Runtime tests use fake package bytes and allocators to cover default fallback, exact frame deadlines, unsigned timer wrap, pause/resume without advancing hidden time, screen-off, page navigation, live generation preparation, swap only after a complete decoded frame, malformed active package, decode failure after installation, one bounded diagnostic, PSRAM allocation failure, and close releasing package/frame buffers once.

- [ ] **Step 2: Run layout/runtime tests red**

Run:

```bash
c++ -std=c++17 -Imain tests/codex_status_ui_layout_test.cpp -o /tmp/codex_status_ui_layout_test && /tmp/codex_status_ui_layout_test
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: FAIL on old 110×120 geometry and missing runtime.

- [ ] **Step 3: Implement PSRAM-backed LVGL image ownership and live swap**

```cpp
class CodexPetRuntime {
public:
    bool open(const codex_pet::PetSelection& selection);
    bool prepare_generation(const codex_pet::PetSelection& selection);
    void set_visible(bool visible, uint32_t now_ms);
    void advance(uint32_t now_ms);
    const lv_image_dsc_t* current_image() const;
    bool take_frame_changed();
    void close();
};
```

Allocate compressed package bytes and the normal `144 * 156 * 3` frame buffer with `heap_caps_malloc(codex_pet::kRgb565A8Bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`. During custom-to-custom replacement only, allocate one second buffer plus the new bounded package; decode the first new frame there, swap descriptors on the LVGL thread, and immediately free the old buffer/package. If either transient allocation fails, keep the old animation. Initialize each modifiable `lv_image_dsc_t` as `LV_COLOR_FORMAT_RGB565A8`, width 144, height 156, and matching stride/data size. Fall back to existing compiled frame arrays on load/decode/timing failure. Cancel the existing LVGL pet timer on close and avoid transport-owned pointers.

- [ ] **Step 4: Apply layout constants and UI lifecycle hooks**

Set the locked coordinates from the spec; remove scaling for custom 144×156 frames; enlarge built-in frames to the same box with nearest-neighbor LVGL scaling and center them. Call `set_visible` from Codex page visibility/display state; call `prepare_generation` only after observing a changed immutable store generation; call `close` during the existing Codex app teardown. Keep all right-button and swipe handlers unchanged.

- [ ] **Step 5: Run focused UI/runtime tests**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_codex_status_f_layout_contract.py -v
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
```

Expected: exact geometry and runtime lifecycle tests PASS with existing gesture tests unchanged.

- [ ] **Step 6: Commit runtime and layout**

```bash
git add main/apps/app_codex_status/codex_pet_runtime.h main/apps/app_codex_status/codex_pet_runtime.cpp main/apps/app_codex_status/codex_status_ui_layout.h main/apps/app_codex_status/app_codex_status.h main/apps/app_codex_status/app_codex_status.cpp tests/codex_pet_runtime_test.cpp tests/codex_status_ui_layout_test.cpp mac-codex-agent/tests/test_codex_status_f_layout_contract.py scripts/run_unified_offline_validation.sh mac-codex-agent/tests/test_unified_voice_validation.py
git commit -m "feat: render custom pets in Codex status"
```

### Task 8: Agent BLE Pet Channel, Upload State Machine, and Cache

**Files:**
- Create: `mac-codex-agent/pet_upload.py`
- Create: `mac-codex-agent/pet_cache.py`
- Create: `mac-codex-agent/tests/test_pet_upload.py`
- Create: `mac-codex-agent/tests/test_pet_cache.py`
- Modify: `mac-codex-agent/agent.py`
- Modify: `mac-codex-agent/runtime.py`
- Modify: `mac-codex-agent/tests/test_agent.py`
- Modify: `mac-codex-agent/tests/test_runtime.py`

**Interfaces:**
- Consumes: Task 4 Python protocol and Task 1 `PetPackage`.
- Produces: `PetCapability`, `PetWatchState`, `PetProgress`, `PetRequest`, `PetUploader.query/upload/cancel/restore`, runtime `submit_pet_request(request)`, controller `upload_pet(package, callback)`, `cancel_pet(request_id)`, `restore_default(callback)`, and `pet_snapshot()`.
- Transport adds `enable_pet_notifications(handler)`, `write_pet_control(payload)`, and `disable_pet_notifications()` using UUIDs `7a23000c-5474-4f50-5741-544348434f44` and `7a23000d-5474-4f50-5741-544348434f44`.

Use these exact immutable models:

```python
class PetCapability(enum.Enum):
    OFFLINE = "offline"
    UNSUPPORTED = "unsupported"
    AUTHENTICATING = "authenticating"
    READY = "ready"

@dataclasses.dataclass(frozen=True)
class PetWatchState:
    watch_id: Optional[bytes]
    capability: PetCapability
    source: str
    schema_min: int
    schema_max: int
    generation: int
    active_hash: bytes
    usable: bool

@dataclasses.dataclass(frozen=True)
class PetProgress:
    request_id: str
    phase: str
    acknowledged_bytes: int
    total_bytes: int
    error_code: str = ""
```

- [ ] **Step 1: Write async uploader and optional-channel isolation tests**

```python
async def test_upload_counts_only_acknowledged_bytes(self):
    transport = FakePetTransport(mtu=247, drop_first_progress=True)
    uploader = PetUploader(transport, KEY, EPOCH, NONCE_FACTORY)
    states = []
    result = await uploader.upload(PACKAGE, states.append)
    self.assertEqual(result.active_hash, PACKAGE.sha256)
    self.assertEqual(states[-1].acknowledged_bytes, len(PACKAGE.encoded))

async def test_missing_service_isolated_from_connection(self):
    transport = FakeBleClient(services_without_pet=True)
    await transport.connect()
    self.assertEqual(await transport.pet_capability(), PetCapability.UNSUPPORTED)
    self.assertTrue(transport.status_channel_alive)
    self.assertTrue(transport.input_channel_alive)
```

Cover query state, unsupported firmware, offline send rejection, authenticated gating, MTU-derived chunks capped at 480, lost acknowledgement retry with exact duplicate, timeout/cancel, disconnect, credential epoch replacement, stale callback suppression, conflicting status, commit exactly once, Restore, close-window continuation, explicit Cancel, Agent quit, and progress-state ordering.

- [ ] **Step 2: Run focused tests red**

Run: `python3 -m unittest mac-codex-agent/tests/test_pet_upload.py mac-codex-agent/tests/test_pet_cache.py mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py -v`

Expected: FAIL on missing upload/cache interfaces.

- [ ] **Step 3: Implement transport and uploader state machine**

```python
@dataclasses.dataclass(frozen=True)
class PetRequest:
    request_id: str
    operation: PetOperation
    package: Optional[PetPackage]
    callback: Callable[[PetProgress], None]

class PetUploader:
    async def query(self) -> PetWatchState:
        return await self._exchange_query()

    async def upload(self, package: PetPackage, progress) -> PetWatchState:
        return await self._run_upload(package, progress)

    async def cancel(self, request_id: str) -> None:
        await self._exchange_cancel(request_id)

    async def restore(self, progress) -> PetWatchState:
        return await self._exchange_restore(progress)
```

Implement these exact methods without the ellipsis bodies: subscribe first, issue authenticated Query, choose `min(480, negotiated_mtu - protocol_overhead)`, send one chunk at a time, advance only from matching Progress acknowledgement, retry only the same offset/ciphertext after a lost acknowledgement, never retry Commit after an ambiguous result, and re-query watch state after reconnect. Runtime owns one bounded request slot; a second request receives `busy`. Credential replacement cancels the old uploader before updating key/epoch. Optional service errors update only pet capability.

- [ ] **Step 4: Implement authoritative local cache**

```python
class PetPreviewCache:
    def store(self, watch_id: bytes, package: PetPackage) -> None:
        self._publish_atomic(self._cache_path(watch_id, package.sha256), package.encoded)

    def load(self, watch_id: bytes, active_hash: bytes) -> Optional[CachedPreview]:
        return self._load_verified(self._cache_path(watch_id, active_hash), active_hash)

    def remove_watch(self, watch_id: bytes) -> None:
        self._remove_bounded(self._watch_path(watch_id))
```

Use `~/Library/Application Support/StopWatch Codex Agent/Pets/<watch-id>/<sha256>/` with the canonical package plus metadata, write into a sibling temporary directory, fsync files, then atomically rename. Reconstruct preview RGBA frames from the verified package palette/indices on read. Never show a cached preview unless both watch identity and authoritative active hash match. Do not store the source GIF.

- [ ] **Step 5: Run Agent runtime, upload, and cache tests**

Run: `python3 -m unittest mac-codex-agent/tests/test_pet_upload.py mac-codex-agent/tests/test_pet_cache.py mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py -v`

Expected: all focused tests PASS and existing reconnect/voice/input cleanup counts remain unchanged.

- [ ] **Step 6: Commit Agent transfer integration**

```bash
git add mac-codex-agent/pet_upload.py mac-codex-agent/pet_cache.py mac-codex-agent/agent.py mac-codex-agent/runtime.py mac-codex-agent/tests/test_pet_upload.py mac-codex-agent/tests/test_pet_cache.py mac-codex-agent/tests/test_agent.py mac-codex-agent/tests/test_runtime.py
git commit -m "feat: upload Codex pets from Agent runtime"
```

### Task 9: Dedicated AppKit Codex Pet Window

**Files:**
- Create: `mac-codex-agent/pet_window.py`
- Create: `mac-codex-agent/tests/test_pet_window.py`
- Modify: `mac-codex-agent/menu_app.py`
- Modify: `mac-codex-agent/tests/test_menu_app.py`

**Interfaces:**
- Consumes: Task 2 conversion output and Task 8 controller methods/snapshot.
- Produces: menu action `Codex Pet...`, one retained `CodexPetWindowController`, drag/drop and file-picker selection, exact converted animation preview, status/progress rendering, Send, Cancel, and Restore Default actions.

- [ ] **Step 1: Write controller/view-model tests before creating AppKit objects**

```python
def test_send_enablement_requires_package_and_authenticated_capability(self):
    model = PetWindowModel()
    model = model.with_conversion(CONVERTED)
    self.assertFalse(model.can_send)
    model = model.with_watch_state(AUTHENTICATED_SUPPORTED_STATE)
    self.assertTrue(model.can_send)

def test_unknown_remote_hash_does_not_reuse_wrong_preview(self):
    model = PetWindowModel().with_watch_state(CUSTOM_HASH_FROM_OTHER_AGENT)
    self.assertEqual(model.headline, "Custom pet installed on watch")
    self.assertIsNone(model.preview)
```

Cover initial/offline/processing/ready/sending/verifying/applying/installed/error/unsupported/busy states, selected wrong extension, converter error mapping, determinate acknowledged progress, Cancel visibility, Restore enablement, window close preserving background transfer, and callback delivery after the window reopens.

- [ ] **Step 2: Run menu/window tests red**

Run: `python3 -m unittest mac-codex-agent/tests/test_pet_window.py mac-codex-agent/tests/test_menu_app.py -v`

Expected: FAIL on missing menu item/window model.

- [ ] **Step 3: Implement the model and independent compact window**

```python
class CodexPetWindowController(NSWindowController):
    def initWithAgentController_(self, agent_controller):
        return self._build_window(agent_controller)

    def chooseGif_(self, sender):
        self._present_gif_picker()

    def sendToWatch_(self, sender):
        self._submit_selected_package()

    def cancelTransfer_(self, sender):
        self._cancel_active_request()

    def restoreDefault_(self, sender):
        self._submit_restore()

    def applySnapshot_(self, snapshot):
        self._render_model(self._model.with_snapshot(snapshot))
```

Implement these exact selectors with real bodies. Build a fixed compact resizable AppKit window containing a 220-point round-watch preview, title/metadata labels, registered `.gif` drop target, choose button, connection line, determinate progress bar, secondary Restore/Cancel action, and primary Send action. Convert on a daemon worker, marshal UI mutations through `AppHelper.callAfter`, animate preview from `ConvertedPet.preview_rgba` and package delays, stop the window timer when hidden, and retain transfer state in the controller/runtime rather than the view.

- [ ] **Step 4: Add the menu entry and lifecycle ownership**

Add `Codex Pet...` immediately before the connection-actions separator. `openCodexPet_` lazily creates one window controller and brings the same window forward on later clicks. App quit stops the preview timer and asks runtime for bounded pet-channel cleanup through its existing shutdown sequence.

- [ ] **Step 5: Run UI model/menu tests and a local window smoke launch**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_pet_window.py mac-codex-agent/tests/test_menu_app.py -v
PYTHONPATH=mac-codex-agent python3 mac-codex-agent/menu_app.py
```

Expected: tests PASS; menu opens one window; supplied GIF previews smoothly; metadata shows 6 frames and 1.08 s; offline Send is disabled; closing and reopening does not duplicate the window. Quit the smoke app from its menu after inspection.

- [ ] **Step 6: Commit the Agent window**

```bash
git add mac-codex-agent/pet_window.py mac-codex-agent/menu_app.py mac-codex-agent/tests/test_pet_window.py mac-codex-agent/tests/test_menu_app.py
git commit -m "feat: add Codex pet upload window"
```

### Task 10: Packaging, Validation Contracts, and Documentation

**Files:**
- Modify: `mac-codex-agent/install_launch_agent.sh`
- Modify: `mac-codex-agent/tests/test_install_script.py`
- Modify: `mac-codex-agent/README.md`
- Modify: `VALIDATION.md`
- Modify: `scripts/run_unified_offline_validation.sh`
- Modify: `mac-codex-agent/tests/test_unified_voice_validation.py`

**Interfaces:**
- Consumes: every new production Python module and host test from Tasks 1–9.
- Produces: an installed self-contained Agent containing Pillow and all pet modules, honest exact host-stage contracts, and operator instructions for upload/restore/unsupported firmware/persistence.

- [ ] **Step 1: Make installer and validation tests fail on missing pet artifacts**

```python
def test_installer_packages_all_pet_modules(self):
    script = INSTALL_SCRIPT.read_text()
    for module in ("pet_package.py", "pet_converter.py", "pet_protocol.py", "pet_upload.py", "pet_cache.py", "pet_window.py"):
        self.assertIn(module, script)

def test_pillow_is_present_in_self_contained_bundle(self):
    self.assertIn('python -c "from PIL import Image"', normalized_installer)
```

Update unified validation expectations with the exact new C++ compile/run stage names and source lists; do not only increment a stage count.

- [ ] **Step 2: Run contract tests red**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_install_script.py mac-codex-agent/tests/test_unified_voice_validation.py -v
./scripts/run_unified_offline_validation.sh --contract
```

Expected: FAIL until packaging and exact stage declarations include the new files.

- [ ] **Step 3: Package modules and add installed-runtime smoke checks**

Add each Python module to the installer's explicit copy list. Extend the installed-bundle smoke command to import `PIL.Image`, `pet_package`, `pet_converter`, `pet_protocol`, `pet_upload`, `pet_cache`, and `pet_window`, and to convert a generated two-frame temporary GIF without accessing repository paths. Preserve atomic publication, rollback, universal2 validation, code signing, and key-material scanning.

- [ ] **Step 4: Document exact user and recovery behavior**

Document: `Codex Pet...` entry; input/output limits; exact-package preview; authenticated connection requirement; `Firmware update required`; Send/Cancel/Restore; persistence across reboot and ordinary flash; built-in fallback; shared-storage reserve error; and that full storage erase removes the pet. Add validation commands and separate package, firmware-build, flash, boot, transport, and physical-display evidence.

- [ ] **Step 5: Run packaging and contract verification**

Run:

```bash
python3 -m unittest mac-codex-agent/tests/test_install_script.py mac-codex-agent/tests/test_unified_voice_validation.py -v
./scripts/run_unified_offline_validation.sh --contract
git diff --check
```

Expected: all tests PASS, contract stage names match execution exactly, and diff check is clean.

- [ ] **Step 6: Commit packaging and docs**

```bash
git add mac-codex-agent/install_launch_agent.sh mac-codex-agent/tests/test_install_script.py mac-codex-agent/README.md VALIDATION.md scripts/run_unified_offline_validation.sh mac-codex-agent/tests/test_unified_voice_validation.py
git commit -m "docs: package and validate Codex pet uploads"
```

### Task 11: Release Gates and Physical Acceptance

**Files:**
- Verify only: all files changed in Tasks 1–10
- Record final evidence in: `VALIDATION.md`

**Interfaces:**
- Consumes: completed feature branch.
- Produces: evidence for source, host behavior, clean firmware build, installed Agent bundle, flash/boot, authenticated upload, persistence, recovery, and AMOLED fidelity.

- [ ] **Step 1: Run all deterministic Agent tests and inspect the exact diff**

Run:

```bash
python3 -m unittest discover -s mac-codex-agent/tests -p 'test_*.py'
git diff --check
git status --short
git diff 99a6bc6..HEAD --stat
```

Expected: all Python tests PASS; only feature/design/plan files appear; no `.superpowers`, generated build directories, credentials, source GIF, or unrelated workspace files are staged.

- [ ] **Step 2: Run the real host stages and clean ESP-IDF build**

Run:

```bash
IDF_PATH=/Users/luke/esp/esp-idf ./scripts/run_unified_offline_validation.sh --host-tests-only
zsh -lc 'source /Users/luke/esp/esp-idf/export.sh >/dev/null && idf.py fullclean build'
```

Expected: every declared host binary compiles/runs; ESP-IDF 5.5.4 clean configure/build succeeds; app and partition capacity gates pass.

- [ ] **Step 3: Install and verify the actual Agent bundle**

Run: `./mac-codex-agent/install_launch_agent.sh`

Then inspect the installed application, restart it, and verify logs reach `Agent started`, authenticated watch state, BLE connected, and pet capability supported. If macOS shows the expected Keychain authorization dialog after signing, choose `Always Allow`, then confirm startup from logs. Inspect installed modules rather than repository source.

- [ ] **Step 4: Flash without firmware backup and verify boot**

Use the repository capacity-gated candidate artifacts and the exact connected `/dev/cu.usbmodem*` device. Write only bootloader, partition table, application, and assets at their declared offsets; do not write or erase the `storage` partition. Verify written hashes and capture a new serial boot showing storage mount, pet selection validation, and BLE service startup.

- [ ] **Step 5: Perform the real supplied-GIF acceptance sequence**

Open `Codex Pet...`, select `/var/folders/39/nvts3ng12vv7cf8yx9460krh0000gn/T/codex-clipboard-18ecce5e-fd00-4e92-a4f1-373567bffc74.gif`, and verify preview metadata is 6 frames, 1.08 s, 144×156, under 256 KiB. Send over the authenticated Production link and observe Processing → Sending → Verifying → Applying → Installed. Confirm the watch swaps at a frame boundary with no blank image.

- [ ] **Step 6: Verify persistence and failure recovery on hardware**

Reboot and confirm the same authoritative hash and animation. Perform one ordinary firmware flash and confirm persistence. Interrupt one replacement transfer and confirm the prior pet remains. Fill shared storage enough to leave less than package size plus 128 KiB and confirm precise rejection without changing the active pet. Restore Default and confirm the built-in robot is selected before custom slots are reclaimed.

- [ ] **Step 7: Complete the physical AMOLED comparison**

Compare default, custom, unavailable/offline Agent, and active-transfer states region by region. Record: pet box 144×156 at `(55,88)`, aspect-fit transparency, pixel sharpness, cadence, no circular clipping, beacon at x210, telemetry x231, quota y246, divider y282, labels/counts y294/y320, two-line detail y363, page dots y416, battery y432, and unchanged right-button/swipe behavior. If the panel exposes optical misalignment, adjust only middle-content coordinates while retaining locked box/header/footer, rebuild, reflash, and repeat this step.

- [ ] **Step 8: Record evidence and make the release-evidence commit**

Add exact command results, artifact hashes, installed-bundle evidence, flash offsets/hashes, boot evidence, active package hash, persistence/failure results, and the named physical comparison packet to `VALIDATION.md`.

```bash
git add VALIDATION.md
git commit -m "test: record Codex pet acceptance evidence"
```
