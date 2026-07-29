#include "agent_registry_store.h"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(ESP_PLATFORM)
#include <nvs.h>
#endif

namespace secure_pairing {

const char kRegistrySlotA[] = "registry_a";
const char kRegistrySlotB[] = "registry_b";
const char kRegistrySelector[] = "registry_sel";

namespace {

#if defined(ESP_PLATFORM)
constexpr char kNvsNamespace[] = "codex_agents";
#endif
constexpr uint16_t kRegistryVersion = 1;
constexpr size_t kDigestSize = 32;
constexpr size_t kSlotHeaderSize = 8;
constexpr size_t kSelectorSize = 8;
constexpr size_t kRecordSize =
    crypto::kPublicKeySize + kAgentFingerprintBytes + 1 +
    kAgentNameBytes + 8;
constexpr size_t kPayloadBaseSize =
    16 + kWatchMasterKeyBytes + 4 + 1 + kAgentFingerprintBytes + 1;
constexpr size_t kMaxPayloadSize =
    kPayloadBaseSize + kMaxAgents * kRecordSize;
constexpr size_t kMaxSlotSize =
    kSlotHeaderSize + kMaxPayloadSize + kDigestSize;

class Writer {
public:
    Writer(uint8_t* output, size_t capacity)
        : _output(output), _capacity(capacity)
    {
    }

    bool bytes(const uint8_t* value, size_t size)
    {
        if (value == nullptr || _size > _capacity ||
            size > _capacity - _size) {
            return false;
        }
        std::copy_n(value, size, _output + _size);
        _size += size;
        return true;
    }

    bool byte(uint8_t value)
    {
        return bytes(&value, 1);
    }

    bool u16(uint16_t value)
    {
        const std::array<uint8_t, 2> encoded = {
            static_cast<uint8_t>((value >> 8U) & 0xffU),
            static_cast<uint8_t>(value & 0xffU),
        };
        return bytes(encoded.data(), encoded.size());
    }

    bool u32(uint32_t value)
    {
        const std::array<uint8_t, 4> encoded = {
            static_cast<uint8_t>((value >> 24U) & 0xffU),
            static_cast<uint8_t>((value >> 16U) & 0xffU),
            static_cast<uint8_t>((value >> 8U) & 0xffU),
            static_cast<uint8_t>(value & 0xffU),
        };
        return bytes(encoded.data(), encoded.size());
    }

    bool i64(int64_t value)
    {
        const uint64_t encoded = static_cast<uint64_t>(value);
        for (int shift = 56; shift >= 0; shift -= 8) {
            if (!byte(
                    static_cast<uint8_t>(
                        (encoded >> static_cast<unsigned>(shift)) & 0xffU))) {
                return false;
            }
        }
        return true;
    }

    size_t size() const
    {
        return _size;
    }

private:
    uint8_t* _output = nullptr;
    size_t _capacity = 0;
    size_t _size = 0;
};

class Reader {
public:
    Reader(const uint8_t* input, size_t size) : _input(input), _size(size) {}

    bool bytes(uint8_t* output, size_t size)
    {
        if (output == nullptr || _offset > _size || size > _size - _offset) {
            return false;
        }
        std::copy_n(_input + _offset, size, output);
        _offset += size;
        return true;
    }

    bool byte(uint8_t& value)
    {
        return bytes(&value, 1);
    }

    bool u16(uint16_t& value)
    {
        std::array<uint8_t, 2> encoded = {};
        if (!bytes(encoded.data(), encoded.size())) {
            return false;
        }
        value =
            static_cast<uint16_t>(
                (static_cast<uint16_t>(encoded[0]) << 8U) | encoded[1]);
        return true;
    }

    bool u32(uint32_t& value)
    {
        std::array<uint8_t, 4> encoded = {};
        if (!bytes(encoded.data(), encoded.size())) {
            return false;
        }
        value = (static_cast<uint32_t>(encoded[0]) << 24U) |
                (static_cast<uint32_t>(encoded[1]) << 16U) |
                (static_cast<uint32_t>(encoded[2]) << 8U) | encoded[3];
        return true;
    }

    bool i64(int64_t& value)
    {
        uint64_t encoded = 0;
        for (size_t index = 0; index < 8; ++index) {
            uint8_t byte_value = 0;
            if (!byte(byte_value)) {
                return false;
            }
            encoded = (encoded << 8U) | byte_value;
        }
        value = static_cast<int64_t>(encoded);
        return true;
    }

    bool done() const
    {
        return _offset == _size;
    }

private:
    const uint8_t* _input = nullptr;
    size_t _size = 0;
    size_t _offset = 0;
};

struct StoredSlot {
    bool present = false;
    bool valid = false;
    uint32_t generation = 0;
    AgentRegistry registry = {};
};

struct Selector {
    bool present = false;
    bool valid = false;
    uint8_t slot = 0;
    uint32_t generation = 0;
};

size_t record_name_size(const AgentRecord& record)
{
    const auto end = std::find(
        record.display_name.begin(), record.display_name.end(), '\0');
    return static_cast<size_t>(end - record.display_name.begin());
}

bool encode_payload(
    const AgentRegistry& registry,
    uint8_t* output,
    size_t capacity,
    size_t& size)
{
    size = 0;
    if (!registry.initialized() || registry.count() > kMaxAgents) {
        return false;
    }
    Writer writer(output, capacity);
    if (!writer.bytes(registry.watch_id().data(), registry.watch_id().size()) ||
        !writer.bytes(
            registry.master_key().data(), registry.master_key().size()) ||
        !writer.u32(registry.key_epoch()) ||
        !writer.byte(registry.has_last_selected() ? 1U : 0U) ||
        !writer.bytes(
            registry.last_selected().data(), registry.last_selected().size()) ||
        !writer.byte(static_cast<uint8_t>(registry.count()))) {
        return false;
    }
    for (size_t index = 0; index < registry.count(); ++index) {
        const AgentRecord& record = registry.records()[index];
        const size_t name_size = record_name_size(record);
        if (name_size == 0 || name_size > kAgentNameBytes ||
            !writer.bytes(record.public_key.data(), record.public_key.size()) ||
            !writer.bytes(record.fingerprint.data(), record.fingerprint.size()) ||
            !writer.byte(static_cast<uint8_t>(name_size)) ||
            !writer.bytes(
                reinterpret_cast<const uint8_t*>(record.display_name.data()),
                kAgentNameBytes) ||
            !writer.i64(record.last_connected_at)) {
            return false;
        }
    }
    size = writer.size();
    return true;
}

bool decode_payload(
    const uint8_t* input,
    size_t size,
    AgentRegistry& registry)
{
    RegistrySnapshot snapshot = {};
    Reader reader(input, size);
    uint8_t selected = 0;
    uint8_t count = 0;
    if (!reader.bytes(snapshot.watch_id.data(), snapshot.watch_id.size()) ||
        !reader.bytes(snapshot.master_key.data(), snapshot.master_key.size()) ||
        !reader.u32(snapshot.key_epoch) || !reader.byte(selected) ||
        selected > 1 ||
        !reader.bytes(
            snapshot.last_selected.data(), snapshot.last_selected.size()) ||
        !reader.byte(count) || count > kMaxAgents) {
        return false;
    }
    snapshot.has_last_selected = selected != 0;
    snapshot.count = count;
    for (size_t index = 0; index < snapshot.count; ++index) {
        AgentRecord& record = snapshot.records[index];
        uint8_t name_size = 0;
        if (!reader.bytes(record.public_key.data(), record.public_key.size()) ||
            !reader.bytes(record.fingerprint.data(), record.fingerprint.size()) ||
            !reader.byte(name_size) || name_size == 0 ||
            name_size > kAgentNameBytes ||
            !reader.bytes(
                reinterpret_cast<uint8_t*>(record.display_name.data()),
                kAgentNameBytes) ||
            !reader.i64(record.last_connected_at)) {
            return false;
        }
        record.display_name[name_size] = '\0';
        std::fill(
            record.display_name.begin() +
                static_cast<std::ptrdiff_t>(name_size + 1),
            record.display_name.end(),
            '\0');
    }
    return reader.done() &&
           AgentRegistry::restore(snapshot, registry) == RegistryResult::Ok;
}

bool digest(
    const uint8_t* input,
    size_t size,
    std::array<uint8_t, kDigestSize>& output)
{
    return input != nullptr && size != 0 &&
           mbedtls_sha256(input, size, output.data(), 0) == 0;
}

StoredSlot read_slot(RegistryStorageBackend& backend, const char* key)
{
    StoredSlot result = {};
    std::array<uint8_t, kMaxSlotSize> encoded = {};
    size_t size = encoded.size();
    const StorageIoResult io = backend.read(key, encoded.data(), size);
    if (io == StorageIoResult::NotFound) {
        return result;
    }
    result.present = true;
    if (io != StorageIoResult::Ok ||
        size < kSlotHeaderSize + kDigestSize) {
        return result;
    }
    Reader reader(encoded.data(), size);
    uint16_t version = 0;
    uint16_t payload_size = 0;
    if (!reader.u16(version) || version != kRegistryVersion ||
        !reader.u32(result.generation) || result.generation == 0 ||
        !reader.u16(payload_size) ||
        size != kSlotHeaderSize + payload_size + kDigestSize) {
        return result;
    }
    std::array<uint8_t, kDigestSize> expected = {};
    if (!digest(encoded.data(), kSlotHeaderSize + payload_size, expected) ||
        !std::equal(
            expected.begin(),
            expected.end(),
            encoded.begin() +
                static_cast<std::ptrdiff_t>(kSlotHeaderSize + payload_size)) ||
        !decode_payload(
            encoded.data() + kSlotHeaderSize,
            payload_size,
            result.registry)) {
        return result;
    }
    result.valid = true;
    return result;
}

Selector read_selector(RegistryStorageBackend& backend)
{
    Selector result = {};
    std::array<uint8_t, kSelectorSize> encoded = {};
    size_t size = encoded.size();
    const StorageIoResult io =
        backend.read(kRegistrySelector, encoded.data(), size);
    if (io == StorageIoResult::NotFound) {
        return result;
    }
    result.present = true;
    if (io != StorageIoResult::Ok || size != encoded.size() ||
        encoded[0] != 'S' || encoded[1] != 'W' ||
        encoded[2] != kRegistryVersion || encoded[3] > 1) {
        return result;
    }
    result.slot = encoded[3];
    result.generation =
        (static_cast<uint32_t>(encoded[4]) << 24U) |
        (static_cast<uint32_t>(encoded[5]) << 16U) |
        (static_cast<uint32_t>(encoded[6]) << 8U) | encoded[7];
    result.valid = result.generation != 0;
    return result;
}

bool encode_slot(
    const AgentRegistry& registry,
    uint32_t generation,
    std::array<uint8_t, kMaxSlotSize>& output,
    size_t& size)
{
    std::array<uint8_t, kMaxPayloadSize> payload = {};
    size_t payload_size = 0;
    if (generation == 0 ||
        !encode_payload(
            registry, payload.data(), payload.size(), payload_size) ||
        payload_size > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    Writer writer(output.data(), output.size());
    if (!writer.u16(kRegistryVersion) || !writer.u32(generation) ||
        !writer.u16(static_cast<uint16_t>(payload_size)) ||
        !writer.bytes(payload.data(), payload_size)) {
        return false;
    }
    std::array<uint8_t, kDigestSize> checksum = {};
    if (!digest(output.data(), writer.size(), checksum) ||
        !writer.bytes(checksum.data(), checksum.size())) {
        return false;
    }
    size = writer.size();
    return true;
}

bool verify_slot(
    RegistryStorageBackend& backend,
    const char* key,
    uint32_t generation,
    const AgentRegistry& registry)
{
    const StoredSlot slot = read_slot(backend, key);
    return slot.valid && slot.generation == generation &&
           slot.registry == registry;
}

StorageIoResult write_selector(
    RegistryStorageBackend& backend,
    uint8_t slot,
    uint32_t generation)
{
    const std::array<uint8_t, kSelectorSize> selector = {
        'S',
        'W',
        static_cast<uint8_t>(kRegistryVersion),
        slot,
        static_cast<uint8_t>((generation >> 24U) & 0xffU),
        static_cast<uint8_t>((generation >> 16U) & 0xffU),
        static_cast<uint8_t>((generation >> 8U) & 0xffU),
        static_cast<uint8_t>(generation & 0xffU),
    };
    return backend.write(
        kRegistrySelector, selector.data(), selector.size());
}

int selected_slot(
    const StoredSlot& a,
    const StoredSlot& b,
    const Selector& selector)
{
    if (selector.valid) {
        const StoredSlot& selected = selector.slot == 0 ? a : b;
        if (selected.valid &&
            selected.generation == selector.generation) {
            return selector.slot;
        }
        const StoredSlot& fallback = selector.slot == 0 ? b : a;
        if (fallback.valid) {
            return selector.slot == 0 ? 1 : 0;
        }
        return -1;
    }
    if (a.valid && b.valid) {
        return a.generation >= b.generation ? 0 : 1;
    }
    if (a.valid) {
        return 0;
    }
    if (b.valid) {
        return 1;
    }
    return -1;
}

}  // namespace

AgentRegistryStore::AgentRegistryStore(RegistryStorageBackend& backend)
    : _backend(backend)
{
}

StoreResult AgentRegistryStore::load(AgentRegistry& output)
{
    output = {};
    const StoredSlot a = read_slot(_backend, kRegistrySlotA);
    const StoredSlot b = read_slot(_backend, kRegistrySlotB);
    const Selector selector = read_selector(_backend);
    const int selected = selected_slot(a, b, selector);
    if (selected == 0) {
        output = a.registry;
        return StoreResult::Ok;
    }
    if (selected == 1) {
        output = b.registry;
        return StoreResult::Ok;
    }
    return a.present || b.present || selector.present ? StoreResult::Corrupt
                                                      : StoreResult::NotFound;
}

StoreResult AgentRegistryStore::save(const AgentRegistry& registry)
{
    if (!registry.initialized()) {
        return StoreResult::InvalidRegistry;
    }
    const StoredSlot a = read_slot(_backend, kRegistrySlotA);
    const StoredSlot b = read_slot(_backend, kRegistrySlotB);
    const Selector selector = read_selector(_backend);
    const int active = selected_slot(a, b, selector);
    const uint32_t active_generation =
        active == 0 ? a.generation : active == 1 ? b.generation : 0;
    if (active_generation == std::numeric_limits<uint32_t>::max()) {
        return StoreResult::StorageError;
    }
    const uint8_t inactive = active == 0 ? 1 : 0;
    const char* key = inactive == 0 ? kRegistrySlotA : kRegistrySlotB;
    const uint32_t generation = active_generation + 1;
    std::array<uint8_t, kMaxSlotSize> encoded = {};
    size_t encoded_size = 0;
    if (!encode_slot(
            registry, generation, encoded, encoded_size)) {
        return StoreResult::InvalidRegistry;
    }
    if (_backend.write(key, encoded.data(), encoded_size) !=
            StorageIoResult::Ok ||
        _backend.commit() != StorageIoResult::Ok) {
        return StoreResult::StorageError;
    }
    if (!verify_slot(_backend, key, generation, registry)) {
        return StoreResult::Corrupt;
    }
    if (write_selector(_backend, inactive, generation) !=
            StorageIoResult::Ok ||
        _backend.commit() != StorageIoResult::Ok) {
        return StoreResult::StorageError;
    }
    return StoreResult::Ok;
}

#if defined(ESP_PLATFORM)
NvsRegistryStorageBackend::NvsRegistryStorageBackend()
{
    nvs_handle_t handle = 0;
    _ready =
        nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK;
    _handle = static_cast<uint32_t>(handle);
}

NvsRegistryStorageBackend::~NvsRegistryStorageBackend()
{
    if (_ready) {
        nvs_close(static_cast<nvs_handle_t>(_handle));
    }
}

bool NvsRegistryStorageBackend::ready() const
{
    return _ready;
}

StorageIoResult NvsRegistryStorageBackend::read(
    const char* key,
    uint8_t* output,
    size_t& size)
{
    if (!_ready) {
        return StorageIoResult::Error;
    }
    const esp_err_t error =
        nvs_get_blob(
            static_cast<nvs_handle_t>(_handle), key, output, &size);
    if (error == ESP_OK) {
        return StorageIoResult::Ok;
    }
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        return StorageIoResult::NotFound;
    }
    if (error == ESP_ERR_NVS_INVALID_LENGTH) {
        return StorageIoResult::BufferTooSmall;
    }
    return StorageIoResult::Error;
}

StorageIoResult NvsRegistryStorageBackend::write(
    const char* key,
    const uint8_t* input,
    size_t size)
{
    return _ready &&
                   nvs_set_blob(
                       static_cast<nvs_handle_t>(_handle),
                       key,
                       input,
                       size) == ESP_OK
               ? StorageIoResult::Ok
               : StorageIoResult::Error;
}

StorageIoResult NvsRegistryStorageBackend::commit()
{
    return _ready &&
                   nvs_commit(static_cast<nvs_handle_t>(_handle)) == ESP_OK
               ? StorageIoResult::Ok
               : StorageIoResult::Error;
}
#endif

}  // namespace secure_pairing
