#include "main/apps/app_codex_status/agent_registry.h"
#include "main/apps/app_codex_status/agent_registry_store.h"
#include "main/apps/app_codex_status/secure_pairing_crypto.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Entropy {
    uint8_t next = 1;
    bool fail = false;
};

bool fill_entropy(void* context, uint8_t* output, size_t size)
{
    auto& entropy = *static_cast<Entropy*>(context);
    if (entropy.fail) {
        return false;
    }
    for (size_t index = 0; index < size; ++index) {
        output[index] = entropy.next++;
    }
    return true;
}

secure_pairing::AgentEnrollment enrollment(uint8_t scalar, const char* name)
{
    secure_pairing::crypto::PrivateScalar private_scalar = {};
    private_scalar.back() = scalar;
    secure_pairing::AgentEnrollment result = {};
    assert(secure_pairing::crypto::public_key_from_private(
        private_scalar, result.public_key));
    result.display_name_size = std::strlen(name);
    std::copy_n(name, result.display_name_size, result.display_name.begin());
    result.last_connected_at = scalar;
    return result;
}

class MemoryBackend : public secure_pairing::RegistryStorageBackend {
public:
    bool near_stack(const void* pointer) const
    {
        if (stack_anchor == nullptr || pointer == nullptr) {
            return false;
        }
        const uintptr_t anchor =
            reinterpret_cast<uintptr_t>(stack_anchor);
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        const uintptr_t distance =
            anchor > address ? anchor - address : address - anchor;
        return distance < 64 * 1024;
    }

    secure_pairing::StorageIoResult read(
        const char* key,
        uint8_t* output,
        size_t& size) override
    {
        stack_buffer_seen =
            stack_buffer_seen || (size > 256 && near_stack(output));
        const auto found = committed.find(key);
        if (found == committed.end()) {
            return secure_pairing::StorageIoResult::NotFound;
        }
        if (output == nullptr || size < found->second.size()) {
            size = found->second.size();
            return secure_pairing::StorageIoResult::BufferTooSmall;
        }
        std::copy(found->second.begin(), found->second.end(), output);
        size = found->second.size();
        return secure_pairing::StorageIoResult::Ok;
    }

    secure_pairing::StorageIoResult write(
        const char* key,
        const uint8_t* input,
        size_t size) override
    {
        stack_buffer_seen =
            stack_buffer_seen || (size > 256 && near_stack(input));
        if (fail_write_key == key) {
            return secure_pairing::StorageIoResult::Error;
        }
        staged[key] = std::vector<uint8_t>(input, input + size);
        return secure_pairing::StorageIoResult::Ok;
    }

    secure_pairing::StorageIoResult commit() override
    {
        if (fail_next_commit) {
            fail_next_commit = false;
            staged.clear();
            return secure_pairing::StorageIoResult::Error;
        }
        committed.insert(staged.begin(), staged.end());
        for (const auto& [key, value] : staged) {
            committed[key] = value;
        }
        staged.clear();
        return secure_pairing::StorageIoResult::Ok;
    }

    void corrupt(const char* key)
    {
        assert(!committed[key].empty());
        committed[key].back() ^= 0x80U;
    }

    std::map<std::string, std::vector<uint8_t>> committed;
    std::map<std::string, std::vector<uint8_t>> staged;
    std::string fail_write_key;
    bool fail_next_commit = false;
    const void* stack_anchor = nullptr;
    bool stack_buffer_seen = false;
};

}  // namespace

int main()
{
    using namespace secure_pairing;

    Entropy entropy = {};
    AgentRegistry registry = {};
    assert(
        AgentRegistry::initialize(fill_entropy, &entropy, registry) ==
        RegistryResult::Ok);
    assert(registry.key_epoch() == 1);
    assert(registry.count() == 0);
    assert(std::any_of(
        registry.master_key().begin(),
        registry.master_key().end(),
        [](uint8_t byte) { return byte != 0; }));

    const auto studio = enrollment(1, "Studio Mac");
    assert(registry.add(studio) == RegistryResult::Ok);
    assert(registry.add(studio) == RegistryResult::Duplicate);
    for (uint8_t index = 2; index <= kMaxAgents; ++index) {
        const std::string name = "Agent " + std::to_string(index);
        assert(registry.add(enrollment(index, name.c_str())) == RegistryResult::Ok);
    }
    assert(registry.count() == kMaxAgents);
    assert(registry.add(enrollment(9, "Overflow")) == RegistryResult::Full);

    auto invalid_name = enrollment(9, "Valid");
    invalid_name.display_name_size = 0;
    assert(registry.rename(
               registry.records()[0].fingerprint,
               invalid_name.display_name,
               invalid_name.display_name_size) ==
           RegistryResult::InvalidName);
    invalid_name.display_name_size = kAgentNameBytes;
    invalid_name.display_name.fill('x');
    invalid_name.display_name[5] = '\n';
    assert(registry.add(invalid_name) == RegistryResult::InvalidName);

    const AgentFingerprint selected = registry.records()[3].fingerprint;
    assert(registry.select(selected, 500) == RegistryResult::Ok);
    assert(registry.has_last_selected());
    assert(registry.last_selected() == selected);
    assert(registry.records()[3].last_connected_at == 500);

    const AgentFingerprint removed = registry.records()[2].fingerprint;
    const auto old_key = registry.master_key();
    const uint32_t old_epoch = registry.key_epoch();
    assert(
        registry.remove_and_rotate(
            removed, fill_entropy, &entropy) == RegistryResult::Ok);
    assert(registry.count() == kMaxAgents - 1);
    assert(registry.key_epoch() == old_epoch + 1);
    assert(registry.master_key() != old_key);
    assert(registry.find(removed) == nullptr);

    const auto stable_key = registry.master_key();
    const auto stable_epoch = registry.key_epoch();
    const auto stable_count = registry.count();
    entropy.fail = true;
    assert(
        registry.remove_and_rotate(
            registry.records()[0].fingerprint,
            fill_entropy,
            &entropy) == RegistryResult::EntropyFailure);
    assert(registry.master_key() == stable_key);
    assert(registry.key_epoch() == stable_epoch);
    assert(registry.count() == stable_count);
    entropy.fail = false;

    MemoryBackend backend;
    AgentRegistryStore store(backend);
    AgentRegistry missing = {};
    assert(store.load(missing) == StoreResult::NotFound);
    assert(store.save(registry) == StoreResult::Ok);
    AgentRegistry first_generation = {};
    assert(store.load(first_generation) == StoreResult::Ok);
    assert(first_generation == registry);

    assert(registry.add(enrollment(9, "Replacement")) == RegistryResult::Ok);
    assert(store.save(registry) == StoreResult::Ok);
    AgentRegistry second_generation = {};
    assert(store.load(second_generation) == StoreResult::Ok);
    assert(second_generation == registry);

    backend.corrupt(kRegistrySlotB);
    AgentRegistry recovered = {};
    assert(store.load(recovered) == StoreResult::Ok);
    assert(recovered == first_generation);

    MemoryBackend interrupted_backend;
    AgentRegistryStore interrupted_store(interrupted_backend);
    assert(interrupted_store.save(first_generation) == StoreResult::Ok);
    interrupted_backend.fail_write_key = kRegistrySelector;
    assert(interrupted_store.save(second_generation) == StoreResult::StorageError);
    AgentRegistry after_interruption = {};
    assert(interrupted_store.load(after_interruption) == StoreResult::Ok);
    assert(after_interruption == first_generation);

    uint8_t stack_anchor = 0;
    MemoryBackend bounded_stack_backend;
    bounded_stack_backend.stack_anchor = &stack_anchor;
    AgentRegistryStore bounded_stack_store(bounded_stack_backend);
    assert(bounded_stack_store.save(registry) == StoreResult::Ok);
    AgentRegistry bounded_stack_loaded = {};
    assert(
        bounded_stack_store.load(bounded_stack_loaded) == StoreResult::Ok);
    assert(bounded_stack_loaded == registry);
    assert(!bounded_stack_backend.stack_buffer_seen);
}
