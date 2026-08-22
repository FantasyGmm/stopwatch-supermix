#include "codex_pet_store.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

#if defined(ESP_PLATFORM)
#include <fcntl.h>
#include <nvs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <esp_vfs_fat.h>
#include <esp_heap_caps.h>
#endif

namespace codex_pet {

const char kPetDirectory[] = "/spiflash/codex_pet";
const char* const kPetSlotPaths[2] = {
    "/spiflash/codex_pet/slot0.cxp",
    "/spiflash/codex_pet/slot1.cxp",
};
const char kPetNvsNamespace[] = "cxpet";
const char kPetNvsKey[] = "active";

namespace {

constexpr uint16_t kEnvelopeVersion = 1;
constexpr uint16_t kSupportedSchema = 1;
constexpr size_t kEnvelopeCrcOffset = 52;
constexpr uint16_t kSelectionVersion = 1;
constexpr size_t kSelectionCrcOffset = 48;
constexpr size_t kCxpSchemaOffset = 4;
constexpr size_t kCxpWidthOffset = 8;
constexpr size_t kCxpHeightOffset = 10;
constexpr size_t kCxpFrameCountOffset = 12;
constexpr size_t kCxpDigestOffset = 40;
constexpr size_t kMinimumCxpBytes = 72;

uint16_t read_u16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(bytes[1]) << 8U;
}

uint32_t read_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           static_cast<uint32_t>(bytes[1]) << 8U |
           static_cast<uint32_t>(bytes[2]) << 16U |
           static_cast<uint32_t>(bytes[3]) << 24U;
}

void write_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

uint32_t crc32(const uint8_t* bytes, size_t size)
{
    uint32_t crc = 0xffffffffU;
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask =
                static_cast<uint32_t>(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

bool all_zero(const std::array<uint8_t, 32>& value)
{
    uint8_t combined = 0;
    for (const uint8_t byte : value) {
        combined = static_cast<uint8_t>(combined | byte);
    }
    return combined == 0;
}

std::array<uint8_t, kSlotEnvelopeBytes> encode_envelope(
    uint32_t generation,
    uint32_t length,
    const PetMetadata& metadata)
{
    std::array<uint8_t, kSlotEnvelopeBytes> encoded{};
    encoded[0] = 'C';
    encoded[1] = 'X';
    encoded[2] = 'S';
    encoded[3] = '1';
    write_u16(encoded.data() + 4, kEnvelopeVersion);
    write_u16(
        encoded.data() + 6,
        static_cast<uint16_t>(kSlotEnvelopeBytes));
    write_u32(encoded.data() + 8, generation);
    write_u32(encoded.data() + 12, length);
    write_u16(encoded.data() + 16, metadata.schema);
    write_u16(encoded.data() + 18, 0);
    std::copy(metadata.hash.begin(), metadata.hash.end(), encoded.begin() + 20);
    write_u32(encoded.data() + kEnvelopeCrcOffset, 0);
    write_u32(
        encoded.data() + kEnvelopeCrcOffset,
        crc32(encoded.data(), encoded.size()));
    return encoded;
}

SelectionRecord encode_selection_record(const PetSelection& selection)
{
    SelectionRecord record{};
    auto& encoded = record.encoded;
    encoded[0] = 'C';
    encoded[1] = 'X';
    encoded[2] = 'R';
    encoded[3] = '1';
    write_u16(encoded.data() + 4, kSelectionVersion);
    write_u16(
        encoded.data() + 6,
        static_cast<uint16_t>(encoded.size()));
    encoded[8] = static_cast<uint8_t>(selection.source);
    encoded[9] = selection.slot;
    write_u16(encoded.data() + 10, selection.schema);
    write_u32(encoded.data() + 12, selection.generation);
    std::copy(selection.hash.begin(), selection.hash.end(), encoded.begin() + 16);
    write_u32(encoded.data() + kSelectionCrcOffset, 0);
    write_u32(
        encoded.data() + kSelectionCrcOffset,
        crc32(encoded.data(), encoded.size()));
    return record;
}

bool decode_selection_record(
    const SelectionRecord& record,
    PetSelection& selection)
{
    const auto& encoded = record.encoded;
    auto checked = encoded;
    const uint32_t stored_crc = read_u32(encoded.data() + kSelectionCrcOffset);
    write_u32(checked.data() + kSelectionCrcOffset, 0);
    if (encoded[0] != 'C' || encoded[1] != 'X' ||
        encoded[2] != 'R' || encoded[3] != '1' ||
        read_u16(encoded.data() + 4) != kSelectionVersion ||
        read_u16(encoded.data() + 6) != encoded.size() ||
        stored_crc != crc32(checked.data(), checked.size()) ||
        encoded[8] > static_cast<uint8_t>(Source::Custom) ||
        encoded[9] > 1) {
        return false;
    }

    PetSelection decoded{};
    decoded.source = static_cast<Source>(encoded[8]);
    decoded.slot = encoded[9];
    decoded.schema = read_u16(encoded.data() + 10);
    decoded.generation = read_u32(encoded.data() + 12);
    std::copy_n(
        encoded.data() + 16,
        decoded.hash.size(),
        decoded.hash.begin());
    decoded.usable = true;
    if (decoded.generation == 0) {
        return false;
    }
    if (decoded.source == Source::BuiltIn) {
        if (decoded.schema != 0 || !all_zero(decoded.hash)) {
            return false;
        }
    } else if (decoded.schema == 0 || all_zero(decoded.hash)) {
        return false;
    }
    selection = decoded;
    return true;
}

bool matching_selection(
    const PetSelection& selection,
    uint8_t slot,
    uint32_t generation,
    uint16_t schema,
    const std::array<uint8_t, 32>& hash)
{
    return selection.source == Source::Custom && selection.slot == slot &&
           selection.generation == generation && selection.schema == schema &&
           selection.hash == hash;
}

}  // namespace

struct CodexPetStore::SlotInfo {
    enum class Kind : uint8_t {
        Missing,
        Valid,
        Unsupported,
        Invalid,
        DigestMismatch,
        IoError,
        AllocationError,
    };

    Kind kind = Kind::Missing;
    uint8_t slot = 0;
    uint32_t generation = 0;
    uint32_t length = 0;
    uint16_t schema = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t frame_count = 0;
    std::array<uint8_t, 32> hash{};
};

CodexPetStore::CodexPetStore(
    PetFileSystem& files,
    PetBufferAllocator& allocator,
    PetSelectionStore& selections)
    : _files(files), _allocator(allocator), _selections(selections)
{
}

CodexPetStore::~CodexPetStore()
{
    if (_staging_open) {
        (void)_files.close(_staging_slot);
    }
}

bool CodexPetStore::metadata_valid(
    uint32_t length,
    const PetMetadata& metadata) const
{
    return length >= kMinimumCxpBytes && length <= kMaxPackageBytes &&
           metadata.schema == kSupportedSchema && metadata.width == kWidth &&
           metadata.height == kHeight && metadata.frame_count >= 1 &&
           metadata.frame_count <= kMaxFrames && !all_zero(metadata.hash);
}

CodexPetStore::SlotInfo CodexPetStore::validate_slot(uint8_t slot) const
{
    SlotInfo result{};
    result.slot = slot;
    if (slot > 1) {
        result.kind = SlotInfo::Kind::Invalid;
        return result;
    }
    const auto stored_size = _files.file_size(slot);
    if (!stored_size.has_value()) {
        return result;
    }
    if (*stored_size < kSlotEnvelopeBytes) {
        result.kind = SlotInfo::Kind::Invalid;
        return result;
    }

    std::array<uint8_t, kSlotEnvelopeBytes> envelope{};
    if (!_files.read_at(slot, 0, {envelope.data(), envelope.size()})) {
        result.kind = SlotInfo::Kind::IoError;
        return result;
    }
    auto checked = envelope;
    const uint32_t stored_crc = read_u32(envelope.data() + kEnvelopeCrcOffset);
    write_u32(checked.data() + kEnvelopeCrcOffset, 0);
    result.generation = read_u32(envelope.data() + 8);
    result.length = read_u32(envelope.data() + 12);
    result.schema = read_u16(envelope.data() + 16);
    std::copy_n(
        envelope.data() + 20,
        result.hash.size(),
        result.hash.begin());
    if (envelope[0] != 'C' || envelope[1] != 'X' ||
        envelope[2] != 'S' || envelope[3] != '1' ||
        read_u16(envelope.data() + 4) != kEnvelopeVersion ||
        read_u16(envelope.data() + 6) != kSlotEnvelopeBytes ||
        result.generation == 0 || result.length < kMinimumCxpBytes ||
        result.length > kMaxPackageBytes || result.schema == 0 ||
        read_u16(envelope.data() + 18) != 0 || all_zero(result.hash) ||
        stored_crc != crc32(checked.data(), checked.size()) ||
        *stored_size - kSlotEnvelopeBytes != result.length) {
        result.kind = SlotInfo::Kind::Invalid;
        return result;
    }

    uint8_t* package = _allocator.allocate(result.length);
    if (package == nullptr) {
        result.kind = SlotInfo::Kind::AllocationError;
        return result;
    }
    if (!_files.read_at(
            slot,
            static_cast<uint32_t>(kSlotEnvelopeBytes),
            {package, result.length})) {
        _allocator.release(package);
        result.kind = SlotInfo::Kind::IoError;
        return result;
    }

    const bool header_matches =
        package[0] == 'C' && package[1] == 'X' && package[2] == 'P' &&
        package[3] == '1' &&
        read_u16(package + kCxpSchemaOffset) == result.schema &&
        std::equal(
            result.hash.begin(),
            result.hash.end(),
            package + kCxpDigestOffset);
    if (!header_matches || !verify_digest({package, result.length})) {
        _allocator.release(package);
        result.kind = SlotInfo::Kind::DigestMismatch;
        return result;
    }
    result.width = read_u16(package + kCxpWidthOffset);
    result.height = read_u16(package + kCxpHeightOffset);
    result.frame_count = read_u16(package + kCxpFrameCountOffset);
    if (result.schema != kSupportedSchema) {
        _allocator.release(package);
        result.kind = SlotInfo::Kind::Unsupported;
        return result;
    }

    const ParseResult parsed = parse_package({package, result.length});
    const bool package_valid =
        parsed.error == PackageError::None &&
        result.width == kWidth && result.height == kHeight &&
        result.frame_count == parsed.package.frames.size();
    _allocator.release(package);
    result.kind =
        package_valid ? SlotInfo::Kind::Valid : SlotInfo::Kind::Invalid;
    return result;
}

bool CodexPetStore::persist_selection(const PetSelection& selection)
{
    const SelectionRecord record = encode_selection_record(selection);
    return _selections.stage(record) && _selections.commit();
}

PetSelection CodexPetStore::load_selection_impl()
{
    _selection = {};
    _protected_slot.reset();
    _cleanup_slot.reset();
    if (!_files.ensure_directory()) {
        _loaded = true;
        return _selection;
    }

    SelectionRecord record{};
    PetSelection recorded{};
    const SelectionIoResult loaded = _selections.load(record);
    const bool record_valid =
        loaded == SelectionIoResult::Ok &&
        decode_selection_record(record, recorded);

    if (record_valid && recorded.source == Source::BuiltIn) {
        _selection = recorded;
        for (uint8_t slot = 0; slot < 2; ++slot) {
            (void)_files.remove(slot);
        }
        _loaded = true;
        return _selection;
    }

    if (record_valid && recorded.source == Source::Custom) {
        const SlotInfo selected = validate_slot(recorded.slot);
        if ((selected.kind == SlotInfo::Kind::Valid ||
             selected.kind == SlotInfo::Kind::Unsupported) &&
            matching_selection(
                recorded,
                selected.slot,
                selected.generation,
                selected.schema,
                selected.hash)) {
            _protected_slot = selected.slot;
            if (selected.kind == SlotInfo::Kind::Valid) {
                _selection = recorded;
                _selection.usable = true;
            } else {
                _selection = {};
                _selection.generation = recorded.generation;
                _selection.usable = false;
            }
            const uint8_t inactive = static_cast<uint8_t>(selected.slot ^ 1U);
            const SlotInfo stale = validate_slot(inactive);
            if (stale.kind == SlotInfo::Kind::Valid) {
                _cleanup_slot = inactive;
            } else if (stale.kind == SlotInfo::Kind::Invalid ||
                stale.kind == SlotInfo::Kind::DigestMismatch) {
                (void)_files.remove(inactive);
            }
            _loaded = true;
            return _selection;
        }
    }

    std::array<SlotInfo, 2> slots = {
        validate_slot(0),
        validate_slot(1),
    };
    std::optional<uint8_t> newest{};
    for (uint8_t slot = 0; slot < 2; ++slot) {
        if ((slots[slot].kind == SlotInfo::Kind::Valid ||
             slots[slot].kind == SlotInfo::Kind::Unsupported) &&
            (!newest.has_value() ||
             slots[slot].generation > slots[*newest].generation)) {
            newest = slot;
        } else if (slots[slot].kind == SlotInfo::Kind::Invalid ||
                   slots[slot].kind == SlotInfo::Kind::DigestMismatch) {
            (void)_files.remove(slot);
        }
    }

    if (newest.has_value()) {
        const SlotInfo& chosen = slots[*newest];
        PetSelection recovered{};
        recovered.source = Source::Custom;
        recovered.generation = chosen.generation;
        recovered.schema = chosen.schema;
        recovered.hash = chosen.hash;
        recovered.slot = chosen.slot;
        recovered.usable = true;
        _protected_slot = chosen.slot;
        (void)persist_selection(recovered);
        if (chosen.kind == SlotInfo::Kind::Valid) {
            _selection = recovered;
        } else {
            _selection = {};
            _selection.generation = chosen.generation;
            _selection.usable = false;
        }
        const uint8_t inactive = static_cast<uint8_t>(chosen.slot ^ 1U);
        if (slots[inactive].kind == SlotInfo::Kind::Valid) {
            _cleanup_slot = inactive;
        }
    }
    _loaded = true;
    return _selection;
}

PetSelection CodexPetStore::load_selection()
{
    return _loaded ? _selection : load_selection_impl();
}

StoreResult CodexPetStore::begin(
    uint32_t length,
    const PetMetadata& metadata)
{
    if (_staging) {
        return StoreResult::Busy;
    }
    _required_bytes = 0;
    _available_bytes = 0;
    if (!metadata_valid(length, metadata)) {
        if (length > kMaxPackageBytes) {
            return StoreResult::TooLarge;
        }
        if (metadata.schema != kSupportedSchema) {
            return StoreResult::UnsupportedSchema;
        }
        return StoreResult::InvalidMetadata;
    }
    if (!_files.ensure_directory()) {
        return StoreResult::IoError;
    }
    if (!_loaded) {
        (void)load_selection_impl();
    }
    if (_cleanup_slot.has_value()) {
        return StoreResult::Busy;
    }
    if (_selection.generation == std::numeric_limits<uint32_t>::max()) {
        return StoreResult::GenerationExhausted;
    }

    const uint8_t inactive = _protected_slot.has_value() ?
        static_cast<uint8_t>(*_protected_slot ^ 1U) : 0U;
    if (!_files.remove(inactive)) {
        return StoreResult::IoError;
    }
    uint64_t available = 0;
    if (!_files.free_bytes(available)) {
        return StoreResult::IoError;
    }
    _available_bytes = available;
    _required_bytes =
        static_cast<uint64_t>(length) + kSlotEnvelopeBytes +
        kStorageReserveBytes;
    if (available < _required_bytes) {
        return StoreResult::InsufficientStorage;
    }
    if (!_files.open_staging(inactive)) {
        return StoreResult::IoError;
    }

    _staging = true;
    _staging_open = true;
    _staging_slot = inactive;
    _staging_length = length;
    _written = 0;
    _staging_generation = _selection.generation + 1U;
    _staging_metadata = metadata;
    const auto envelope = encode_envelope(
        _staging_generation, _staging_length, _staging_metadata);
    if (!_files.write_at(
            _staging_slot,
            0,
            {envelope.data(), envelope.size()})) {
        return retire_staging(StoreResult::IoError, true);
    }
    return StoreResult::Ok;
}

StoreResult CodexPetStore::write(uint32_t offset, ByteView bytes)
{
    if (!_staging || !_staging_open || bytes.data == nullptr ||
        bytes.size == 0 || offset > _staging_length ||
        bytes.size > static_cast<size_t>(_staging_length - offset)) {
        return retire_staging(StoreResult::InvalidOffset, true);
    }
    if (offset < _written) {
        if (bytes.size > static_cast<size_t>(_written - offset)) {
            return retire_staging(StoreResult::InvalidOffset, true);
        }
        std::array<uint8_t, 480> comparison{};
        if (bytes.size > comparison.size() ||
            !_files.read_at(
                _staging_slot,
                static_cast<uint32_t>(kSlotEnvelopeBytes + offset),
                {comparison.data(), bytes.size}) ||
            !std::equal(bytes.data, bytes.data + bytes.size, comparison.data())) {
            return retire_staging(StoreResult::InvalidOffset, true);
        }
        return StoreResult::Ok;
    }
    if (offset != _written ||
        !_files.write_at(
            _staging_slot,
            static_cast<uint32_t>(kSlotEnvelopeBytes + offset),
            bytes)) {
        return retire_staging(
            offset == _written ? StoreResult::IoError :
                                 StoreResult::InvalidOffset,
            true);
    }
    _written += static_cast<uint32_t>(bytes.size);
    return StoreResult::Ok;
}

StoreResult CodexPetStore::retire_staging(
    StoreResult result,
    bool remove_file)
{
    if (_staging_open) {
        (void)_files.close(_staging_slot);
        _staging_open = false;
    }
    if (_staging && remove_file) {
        (void)_files.remove(_staging_slot);
    }
    _staging = false;
    _written = 0;
    return result;
}

StoreResult CodexPetStore::commit()
{
    if (!_staging || !_staging_open || _written != _staging_length) {
        return retire_staging(StoreResult::Incomplete, true);
    }
    if (!_files.flush(_staging_slot)) {
        return retire_staging(StoreResult::IoError, true);
    }
    if (!_files.sync(_staging_slot)) {
        return retire_staging(StoreResult::IoError, true);
    }
    const bool closed = _files.close(_staging_slot);
    _staging_open = false;
    if (!closed) {
        return retire_staging(StoreResult::IoError, true);
    }

    const SlotInfo validated = validate_slot(_staging_slot);
    if (validated.kind != SlotInfo::Kind::Valid) {
        StoreResult error = StoreResult::InvalidPackage;
        if (validated.kind == SlotInfo::Kind::IoError) {
            error = StoreResult::IoError;
        } else if (validated.kind == SlotInfo::Kind::AllocationError) {
            error = StoreResult::AllocationFailed;
        } else if (validated.kind == SlotInfo::Kind::Unsupported) {
            error = StoreResult::UnsupportedSchema;
        } else if (validated.kind == SlotInfo::Kind::DigestMismatch) {
            error = StoreResult::DigestMismatch;
        }
        return retire_staging(error, true);
    }
    if (validated.generation != _staging_generation ||
        validated.length != _staging_length ||
        validated.schema != _staging_metadata.schema ||
        validated.width != _staging_metadata.width ||
        validated.height != _staging_metadata.height ||
        validated.frame_count != _staging_metadata.frame_count ||
        validated.hash != _staging_metadata.hash) {
        return retire_staging(StoreResult::DigestMismatch, true);
    }

    PetSelection next{};
    next.source = Source::Custom;
    next.generation = _staging_generation;
    next.schema = validated.schema;
    next.hash = validated.hash;
    next.slot = _staging_slot;
    next.usable = true;
    if (!persist_selection(next)) {
        return retire_staging(StoreResult::SelectionIoError, true);
    }

    if (_protected_slot.has_value() && *_protected_slot != next.slot) {
        _cleanup_slot = _protected_slot;
    }
    _selection = next;
    _protected_slot = next.slot;
    _staging = false;
    _written = 0;
    return StoreResult::Ok;
}

StoreResult CodexPetStore::acknowledge()
{
    if (!_cleanup_slot.has_value()) {
        return StoreResult::Ok;
    }
    const uint8_t slot = *_cleanup_slot;
    if (!_files.remove(slot)) {
        return StoreResult::CleanupWarning;
    }
    _cleanup_slot.reset();
    return StoreResult::Ok;
}

void CodexPetStore::cancel()
{
    (void)retire_staging(StoreResult::Ok, true);
}

StoreResult CodexPetStore::restore_default()
{
    if (_staging) {
        return StoreResult::Busy;
    }
    if (!_loaded) {
        (void)load_selection_impl();
    }
    if (_selection.generation == std::numeric_limits<uint32_t>::max()) {
        return StoreResult::GenerationExhausted;
    }
    PetSelection restored{};
    restored.source = Source::BuiltIn;
    restored.generation = _selection.generation + 1U;
    restored.usable = true;
    if (!persist_selection(restored)) {
        return StoreResult::SelectionIoError;
    }
    _selection = restored;
    _protected_slot.reset();
    _cleanup_slot.reset();
    bool cleaned = true;
    for (uint8_t slot = 0; slot < 2; ++slot) {
        cleaned = _files.remove(slot) && cleaned;
    }
    return cleaned ? StoreResult::Ok : StoreResult::CleanupWarning;
}

uint32_t CodexPetStore::generation() const
{
    return _selection.generation;
}

uint64_t CodexPetStore::required_bytes() const
{
    return _required_bytes;
}

uint64_t CodexPetStore::available_bytes() const
{
    return _available_bytes;
}

#if defined(ESP_PLATFORM)
namespace {

FILE* file_handle(void* handle)
{
    return static_cast<FILE*>(handle);
}

bool seek_file(FILE* file, uint32_t offset)
{
    return file != nullptr &&
           offset <= static_cast<uint32_t>(std::numeric_limits<long>::max()) &&
           std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
}

}  // namespace

FatPetFileSystem::FatPetFileSystem() = default;

FatPetFileSystem::~FatPetFileSystem()
{
    for (uint8_t slot = 0; slot < _handles.size(); ++slot) {
        if (_handles[slot] != nullptr) {
            (void)std::fclose(file_handle(_handles[slot]));
            _handles[slot] = nullptr;
        }
    }
}

bool FatPetFileSystem::ensure_directory()
{
    if (::mkdir(kPetDirectory, 0775) == 0 || errno == EEXIST) {
        struct stat status {};
        return ::stat(kPetDirectory, &status) == 0 && S_ISDIR(status.st_mode);
    }
    return false;
}

bool FatPetFileSystem::free_bytes(uint64_t& output) const
{
    uint64_t total_bytes = 0;
    output = 0;
    return esp_vfs_fat_info("/spiflash", &total_bytes, &output) == ESP_OK;
}

bool FatPetFileSystem::open_staging(uint8_t slot)
{
    if (slot > 1 || _handles[slot] != nullptr) {
        return false;
    }
    _handles[slot] = std::fopen(kPetSlotPaths[slot], "w+b");
    return _handles[slot] != nullptr;
}

bool FatPetFileSystem::write_at(uint8_t slot, uint32_t offset, ByteView bytes)
{
    FILE* file = slot <= 1 ? file_handle(_handles[slot]) : nullptr;
    return bytes.data != nullptr && seek_file(file, offset) &&
           std::fwrite(bytes.data, 1, bytes.size, file) == bytes.size;
}

bool FatPetFileSystem::read_at(
    uint8_t slot,
    uint32_t offset,
    MutableByteView output) const
{
    if (slot > 1 || output.data == nullptr) {
        return false;
    }
    FILE* file = file_handle(_handles[slot]);
    bool owned = false;
    if (file != nullptr) {
        if (std::fflush(file) != 0) {
            return false;
        }
    } else {
        file = std::fopen(kPetSlotPaths[slot], "rb");
        owned = true;
    }
    const bool read =
        seek_file(file, offset) &&
        std::fread(output.data, 1, output.size, file) == output.size;
    if (owned && file != nullptr) {
        (void)std::fclose(file);
    }
    return read;
}

bool FatPetFileSystem::flush(uint8_t slot)
{
    FILE* file = slot <= 1 ? file_handle(_handles[slot]) : nullptr;
    return file != nullptr && std::fflush(file) == 0;
}

bool FatPetFileSystem::sync(uint8_t slot)
{
    FILE* file = slot <= 1 ? file_handle(_handles[slot]) : nullptr;
    return file != nullptr && ::fsync(::fileno(file)) == 0;
}

bool FatPetFileSystem::close(uint8_t slot)
{
    if (slot > 1 || _handles[slot] == nullptr) {
        return false;
    }
    FILE* file = file_handle(_handles[slot]);
    _handles[slot] = nullptr;
    return std::fclose(file) == 0;
}

std::optional<size_t> FatPetFileSystem::file_size(uint8_t slot) const
{
    if (slot > 1) {
        return std::nullopt;
    }
    struct stat status {};
    if (::stat(kPetSlotPaths[slot], &status) != 0 || status.st_size < 0) {
        return std::nullopt;
    }
    return static_cast<size_t>(status.st_size);
}

bool FatPetFileSystem::remove(uint8_t slot)
{
    if (slot > 1) {
        return false;
    }
    if (_handles[slot] != nullptr && !close(slot)) {
        return false;
    }
    return std::remove(kPetSlotPaths[slot]) == 0 || errno == ENOENT;
}

uint8_t* PsramPetBufferAllocator::allocate(size_t bytes)
{
    return static_cast<uint8_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void PsramPetBufferAllocator::release(uint8_t* buffer)
{
    heap_caps_free(buffer);
}

NvsPetSelectionStore::NvsPetSelectionStore()
{
    nvs_handle_t handle = 0;
    _ready =
        nvs_open(kPetNvsNamespace, NVS_READWRITE, &handle) == ESP_OK;
    if (_ready) {
        _handle = handle;
    }
}

NvsPetSelectionStore::~NvsPetSelectionStore()
{
    if (_ready) {
        nvs_close(static_cast<nvs_handle_t>(_handle));
    }
}

bool NvsPetSelectionStore::ready() const
{
    return _ready;
}

SelectionIoResult NvsPetSelectionStore::load(SelectionRecord& record) const
{
    if (!_ready) {
        return SelectionIoResult::Error;
    }
    size_t size = record.encoded.size();
    const esp_err_t result = nvs_get_blob(
        static_cast<nvs_handle_t>(_handle),
        kPetNvsKey,
        record.encoded.data(),
        &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return SelectionIoResult::NotFound;
    }
    return result == ESP_OK && size == record.encoded.size() ?
        SelectionIoResult::Ok : SelectionIoResult::Error;
}

bool NvsPetSelectionStore::stage(const SelectionRecord& record)
{
    return _ready &&
           nvs_set_blob(
               static_cast<nvs_handle_t>(_handle),
               kPetNvsKey,
               record.encoded.data(),
               record.encoded.size()) == ESP_OK;
}

bool NvsPetSelectionStore::commit()
{
    return _ready &&
           nvs_commit(static_cast<nvs_handle_t>(_handle)) == ESP_OK;
}
#endif

}  // namespace codex_pet
