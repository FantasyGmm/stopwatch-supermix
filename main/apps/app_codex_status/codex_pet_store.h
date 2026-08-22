#pragma once

#include "codex_pet_package.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace codex_pet {

constexpr size_t kSlotEnvelopeBytes = 56;
constexpr uint64_t kStorageReserveBytes = 128U * 1024U;
constexpr size_t kSelectionRecordBytes = 52;

extern const char kPetDirectory[];
extern const char* const kPetSlotPaths[2];
extern const char kPetNvsNamespace[];
extern const char kPetNvsKey[];

enum class Source : uint8_t {
    BuiltIn = 0,
    Custom = 1,
};

struct PetMetadata {
    uint16_t schema = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t frame_count = 0;
    std::array<uint8_t, 32> hash{};
};

struct PetSelection {
    Source source = Source::BuiltIn;
    uint32_t generation = 0;
    uint16_t schema = 0;
    std::array<uint8_t, 32> hash{};
    uint8_t slot = 0;
    bool usable = true;
};

enum class StoreResult : uint8_t {
    Ok,
    Busy,
    InvalidMetadata,
    UnsupportedSchema,
    TooLarge,
    InsufficientStorage,
    InvalidOffset,
    Incomplete,
    DigestMismatch,
    InvalidPackage,
    IoError,
    SelectionIoError,
    AllocationFailed,
    GenerationExhausted,
    CleanupWarning,
};

class PetFileSystem {
public:
    virtual ~PetFileSystem() = default;
    virtual bool ensure_directory() = 0;
    virtual bool free_bytes(uint64_t& output) const = 0;
    virtual bool open_staging(uint8_t slot) = 0;
    virtual bool write_at(
        uint8_t slot,
        uint32_t offset,
        ByteView bytes) = 0;
    virtual bool read_at(
        uint8_t slot,
        uint32_t offset,
        MutableByteView output) const = 0;
    virtual bool flush(uint8_t slot) = 0;
    virtual bool sync(uint8_t slot) = 0;
    virtual bool close(uint8_t slot) = 0;
    virtual std::optional<size_t> file_size(uint8_t slot) const = 0;
    virtual bool remove(uint8_t slot) = 0;
};

class PetBufferAllocator {
public:
    virtual ~PetBufferAllocator() = default;
    virtual uint8_t* allocate(size_t bytes) = 0;
    virtual void release(uint8_t* buffer) = 0;
};

struct SelectionRecord {
    std::array<uint8_t, kSelectionRecordBytes> encoded{};
};

enum class SelectionIoResult : uint8_t {
    Ok,
    NotFound,
    Error,
};

class PetSelectionStore {
public:
    virtual ~PetSelectionStore() = default;
    virtual SelectionIoResult load(SelectionRecord& record) const = 0;
    virtual bool stage(const SelectionRecord& record) = 0;
    virtual bool commit() = 0;
};

class CodexPetStore {
public:
    CodexPetStore(
        PetFileSystem& files,
        PetBufferAllocator& allocator,
        PetSelectionStore& selections);
    ~CodexPetStore();

    StoreResult begin(uint32_t length, const PetMetadata& metadata);
    StoreResult write(uint32_t offset, ByteView bytes);
    StoreResult commit();
    StoreResult acknowledge();
    void cancel();
    StoreResult restore_default();
    PetSelection load_selection();

    uint32_t generation() const;
    uint64_t required_bytes() const;
    uint64_t available_bytes() const;

private:
    struct SlotInfo;

    PetSelection load_selection_impl();
    SlotInfo validate_slot(uint8_t slot) const;
    StoreResult retire_staging(StoreResult result, bool remove_file);
    bool persist_selection(const PetSelection& selection);
    bool metadata_valid(uint32_t length, const PetMetadata& metadata) const;

    PetFileSystem& _files;
    PetBufferAllocator& _allocator;
    PetSelectionStore& _selections;
    PetSelection _selection{};
    bool _loaded = false;
    bool _staging = false;
    bool _staging_open = false;
    uint8_t _staging_slot = 0;
    uint32_t _staging_length = 0;
    uint32_t _written = 0;
    uint32_t _staging_generation = 0;
    PetMetadata _staging_metadata{};
    std::optional<uint8_t> _protected_slot{};
    uint8_t _cleanup_mask = 0;
    uint64_t _required_bytes = 0;
    uint64_t _available_bytes = 0;
};

#if defined(ESP_PLATFORM)
class FatPetFileSystem final : public PetFileSystem {
public:
    FatPetFileSystem();
    ~FatPetFileSystem() override;

    bool ensure_directory() override;
    bool free_bytes(uint64_t& output) const override;
    bool open_staging(uint8_t slot) override;
    bool write_at(uint8_t slot, uint32_t offset, ByteView bytes) override;
    bool read_at(
        uint8_t slot,
        uint32_t offset,
        MutableByteView output) const override;
    bool flush(uint8_t slot) override;
    bool sync(uint8_t slot) override;
    bool close(uint8_t slot) override;
    std::optional<size_t> file_size(uint8_t slot) const override;
    bool remove(uint8_t slot) override;

private:
    std::array<void*, 2> _handles{};
};

class PsramPetBufferAllocator final : public PetBufferAllocator {
public:
    uint8_t* allocate(size_t bytes) override;
    void release(uint8_t* buffer) override;
};

class NvsPetSelectionStore final : public PetSelectionStore {
public:
    NvsPetSelectionStore();
    ~NvsPetSelectionStore() override;

    bool ready() const;
    SelectionIoResult load(SelectionRecord& record) const override;
    bool stage(const SelectionRecord& record) override;
    bool commit() override;

private:
    uint32_t _handle = 0;
    bool _ready = false;
};
#endif

}  // namespace codex_pet
