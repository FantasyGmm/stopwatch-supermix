#include "codex_pet_runtime.h"

#if !defined(STOPWATCH_HOST_TEST)
#include "assets/codex_pet_assets.h"
#include <mooncake_log.h>
#endif

#include <algorithm>
#include <array>
#include <limits>

namespace codex_pet {
namespace {

constexpr size_t kCxpMinimumBytes = 72;
constexpr size_t kCxpDigestOffset = 40;

#if defined(STOPWATCH_HOST_TEST)
constexpr uint32_t kBuiltinFrameDurationMs = 250;
const std::array<uint8_t, 3> kHostBuiltinPixels{};
const lv_image_dsc_t kHostBuiltinImage = {
    {LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_RGB565A8, 0, 1, 1, 2, 0},
    static_cast<uint32_t>(kHostBuiltinPixels.size()),
    kHostBuiltinPixels.data(),
    nullptr,
    nullptr,
};

const lv_image_dsc_t* builtin_image(uint16_t)
{
    return &kHostBuiltinImage;
}

uint32_t builtin_frame_duration(uint16_t)
{
    return kBuiltinFrameDurationMs;
}

constexpr uint16_t kBuiltinFrameCount = 1;
#else
const lv_image_dsc_t* builtin_image(uint16_t frame_index)
{
    return codex_pet_idle_frames[frame_index % CODEX_PET_IDLE_FRAME_COUNT];
}

uint32_t builtin_frame_duration(uint16_t frame_index)
{
    return codex_pet_idle_durations_ms[
        frame_index % CODEX_PET_IDLE_FRAME_COUNT];
}

constexpr uint16_t kBuiltinFrameCount = CODEX_PET_IDLE_FRAME_COUNT;
#endif

class PackageFrameDecoder final : public PetFrameDecoder {
public:
    DecodeError decode(
        const PackageView& package,
        uint16_t frame_index,
        MutableByteView output) override
    {
        return decode_frame(package, frame_index, output);
    }
};

PackageFrameDecoder kPackageFrameDecoder;

bool matches_selection(const PetSelection& selection)
{
    return selection.source == Source::Custom && selection.usable &&
           selection.slot < 2 && selection.schema != 0 &&
           std::any_of(
               selection.hash.begin(), selection.hash.end(),
               [](uint8_t byte) { return byte != 0; });
}

}  // namespace

CodexPetRuntime::CodexPetRuntime(
    PetFileSystem& files,
    PetBufferAllocator& allocator,
    PetFrameDecoder* decoder,
    PetRuntimeDiagnosticSink* diagnostics)
    : _files(files),
      _allocator(allocator),
      _decoder(decoder != nullptr ? decoder : &kPackageFrameDecoder),
      _diagnostics(diagnostics)
{
}

CodexPetRuntime::~CodexPetRuntime()
{
    close();
}

bool CodexPetRuntime::open(const PetSelection& selection)
{
    close();
    _diagnostic_reported = false;
    activate_builtin();
    if (!matches_selection(selection)) {
        return true;
    }

    Candidate candidate{};
    if (!load_candidate(selection, candidate)) {
        return true;
    }
    activate_candidate(candidate);
    _diagnostic_reported = false;
    return true;
}

bool CodexPetRuntime::prepare_generation(const PetSelection& selection)
{
    release_pending();
    if (!matches_selection(selection)) {
        activate_builtin();
        _diagnostic_reported = false;
        return true;
    }

    Candidate candidate{};
    if (!load_candidate(selection, candidate)) {
        return false;
    }

    _diagnostic_reported = false;
    if (_using_custom && _visible) {
        _pending = candidate;
        return true;
    }
    activate_candidate(candidate);
    return true;
}

void CodexPetRuntime::set_visible(bool visible, uint32_t now_ms)
{
    if (_visible == visible) {
        return;
    }
    _visible = visible;
    _frame_deadline_armed = visible;
    if (visible) {
        _frame_deadline_ms = now_ms + displayed_frame_duration_ms();
    }
}

void CodexPetRuntime::advance(uint32_t now_ms)
{
    if (!_visible || _current_image == nullptr) {
        return;
    }
    if (!_frame_deadline_armed) {
        _frame_deadline_armed = true;
        _frame_deadline_ms = now_ms + displayed_frame_duration_ms();
        return;
    }

    while (deadline_reached(now_ms, _frame_deadline_ms)) {
        if (_pending.package_bytes != nullptr) {
            swap_pending();
        } else if (_using_custom) {
            const uint16_t next = static_cast<uint16_t>(
                (_frame_index + 1U) % _current.package.frames.size());
            if (!decode_into_current(next)) {
                fallback(PetRuntimeDiagnostic::DecodeFailure);
                _frame_deadline_ms = now_ms + displayed_frame_duration_ms();
                _frame_deadline_armed = true;
                return;
            }
            _frame_index = next;
            _frame_changed = true;
        } else {
            _frame_index = static_cast<uint16_t>(
                (_frame_index + 1U) % kBuiltinFrameCount);
            _current_image = builtin_image(_frame_index);
            _frame_changed = true;
        }
        _frame_deadline_ms += displayed_frame_duration_ms();
    }
}

const lv_image_dsc_t* CodexPetRuntime::current_image() const
{
    return _current_image;
}

bool CodexPetRuntime::take_frame_changed()
{
    const bool changed = _frame_changed;
    _frame_changed = false;
    return changed;
}

bool CodexPetRuntime::using_custom() const
{
    return _using_custom;
}

uint32_t CodexPetRuntime::next_frame_delay_ms() const
{
    return std::max<uint32_t>(displayed_frame_duration_ms(), 1U);
}

void CodexPetRuntime::close()
{
    release_pending();
    release_current_custom();
    _current_image = nullptr;
    _frame_index = 0;
    _frame_deadline_ms = 0;
    _frame_deadline_armed = false;
    _visible = false;
    _using_custom = false;
    _frame_changed = false;
}

bool CodexPetRuntime::load_candidate(
    const PetSelection& selection,
    Candidate& candidate)
{
    const auto stored_size = _files.file_size(selection.slot);
    if (!stored_size.has_value() || *stored_size < kSlotEnvelopeBytes ||
        *stored_size - kSlotEnvelopeBytes < kCxpMinimumBytes ||
        *stored_size - kSlotEnvelopeBytes > kMaxPackageBytes) {
        report_once(PetRuntimeDiagnostic::InvalidPackage);
        return false;
    }
    const size_t package_size = *stored_size - kSlotEnvelopeBytes;
    candidate.package_bytes = _allocator.allocate(package_size);
    if (candidate.package_bytes == nullptr) {
        report_once(PetRuntimeDiagnostic::AllocationFailure);
        return false;
    }
    if (!_files.read_at(
            selection.slot,
            static_cast<uint32_t>(kSlotEnvelopeBytes),
            {candidate.package_bytes, package_size})) {
        release_candidate(candidate);
        report_once(PetRuntimeDiagnostic::InvalidPackage);
        return false;
    }

    const ByteView encoded{candidate.package_bytes, package_size};
    const ParseResult parsed = parse_package(encoded);
    if (parsed.error != PackageError::None ||
        package_size < kCxpDigestOffset + selection.hash.size() ||
        !std::equal(
            selection.hash.begin(), selection.hash.end(),
            candidate.package_bytes + kCxpDigestOffset)) {
        release_candidate(candidate);
        report_once(PetRuntimeDiagnostic::InvalidPackage);
        return false;
    }
    candidate.package = parsed.package;
    candidate.frame_bytes = _allocator.allocate(kRgb565A8Bytes);
    if (candidate.frame_bytes == nullptr) {
        release_candidate(candidate);
        report_once(PetRuntimeDiagnostic::AllocationFailure);
        return false;
    }
    candidate.image = {};
    candidate.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    candidate.image.header.cf = LV_COLOR_FORMAT_RGB565A8;
    candidate.image.header.flags = 0;
    candidate.image.header.w = kWidth;
    candidate.image.header.h = kHeight;
    candidate.image.header.stride = static_cast<uint16_t>(kWidth * 2U);
    candidate.image.header.reserved_2 = 0;
    candidate.image.data_size = static_cast<uint32_t>(kRgb565A8Bytes);
    candidate.image.data = candidate.frame_bytes;
    if (!decode_candidate(candidate, 0)) {
        release_candidate(candidate);
        report_once(PetRuntimeDiagnostic::DecodeFailure);
        return false;
    }
    return true;
}

bool CodexPetRuntime::decode_into_current(uint16_t frame_index)
{
    return decode(
               _current.package,
               frame_index,
               {_current.frame_bytes, kRgb565A8Bytes}) == DecodeError::None;
}

bool CodexPetRuntime::decode_candidate(Candidate& candidate, uint16_t frame_index)
{
    return decode(
               candidate.package,
               frame_index,
               {candidate.frame_bytes, kRgb565A8Bytes}) == DecodeError::None;
}

DecodeError CodexPetRuntime::decode(
    const PackageView& package,
    uint16_t frame_index,
    MutableByteView output)
{
    return _decoder->decode(package, frame_index, output);
}

void CodexPetRuntime::activate_candidate(Candidate& candidate)
{
    release_current_custom();
    _current = candidate;
    candidate = {};
    _current_image = &_current.image;
    _frame_index = 0;
    _frame_deadline_armed = false;
    _using_custom = true;
    _frame_changed = true;
}

void CodexPetRuntime::activate_builtin()
{
    release_current_custom();
    _current_image = builtin_image(0);
    _frame_index = 0;
    _frame_deadline_armed = false;
    _using_custom = false;
    _frame_changed = true;
}

void CodexPetRuntime::swap_pending()
{
    Candidate next = _pending;
    _pending = {};
    activate_candidate(next);
    _frame_deadline_armed = true;
}

void CodexPetRuntime::release_candidate(Candidate& candidate)
{
    if (candidate.frame_bytes != nullptr) {
        _allocator.release(candidate.frame_bytes);
    }
    if (candidate.package_bytes != nullptr) {
        _allocator.release(candidate.package_bytes);
    }
    candidate = {};
}

void CodexPetRuntime::release_current_custom()
{
    release_candidate(_current);
}

void CodexPetRuntime::release_pending()
{
    release_candidate(_pending);
}

void CodexPetRuntime::fallback(PetRuntimeDiagnostic diagnostic)
{
    release_pending();
    activate_builtin();
    report_once(diagnostic);
}

void CodexPetRuntime::report_once(PetRuntimeDiagnostic diagnostic)
{
    if (_diagnostic_reported || diagnostic == PetRuntimeDiagnostic::None) {
        return;
    }
    _diagnostic_reported = true;
    if (_diagnostics != nullptr) {
        _diagnostics->record(diagnostic);
        return;
    }
#if !defined(STOPWATCH_HOST_TEST)
    mclog::tagWarn("CodexPet", "renderer fallback diagnostic: {}",
                   static_cast<unsigned>(diagnostic));
#endif
}

uint32_t CodexPetRuntime::displayed_frame_duration_ms() const
{
    if (_using_custom && _current.package.frames.size() != 0) {
        return _current.package.frames[_frame_index].duration_ms;
    }
    return builtin_frame_duration(_frame_index);
}

bool CodexPetRuntime::deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

}  // namespace codex_pet
