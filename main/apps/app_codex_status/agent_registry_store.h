#pragma once

#include "agent_registry.h"

#include <cstddef>
#include <cstdint>

namespace secure_pairing {

extern const char kRegistrySlotA[];
extern const char kRegistrySlotB[];
extern const char kRegistrySelector[];

enum class StorageIoResult : uint8_t {
    Ok,
    NotFound,
    BufferTooSmall,
    Error,
};

class RegistryStorageBackend {
public:
    virtual ~RegistryStorageBackend() = default;
    virtual StorageIoResult read(
        const char* key,
        uint8_t* output,
        size_t& size) = 0;
    virtual StorageIoResult write(
        const char* key,
        const uint8_t* input,
        size_t size) = 0;
    virtual StorageIoResult commit() = 0;
};

enum class StoreResult : uint8_t {
    Ok,
    NotFound,
    Corrupt,
    InvalidRegistry,
    StorageError,
};

class AgentRegistryStore {
public:
    explicit AgentRegistryStore(RegistryStorageBackend& backend);

    StoreResult load(AgentRegistry& output);
    StoreResult save(const AgentRegistry& registry);

private:
    RegistryStorageBackend& _backend;
};

#if defined(ESP_PLATFORM)
class NvsRegistryStorageBackend : public RegistryStorageBackend {
public:
    NvsRegistryStorageBackend();
    ~NvsRegistryStorageBackend() override;

    bool ready() const;
    StorageIoResult read(
        const char* key,
        uint8_t* output,
        size_t& size) override;
    StorageIoResult write(
        const char* key,
        const uint8_t* input,
        size_t size) override;
    StorageIoResult commit() override;

private:
    uint32_t _handle = 0;
    bool _ready = false;
};
#endif

}  // namespace secure_pairing
