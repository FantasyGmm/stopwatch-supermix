#include "agent_registry.h"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <limits>

namespace secure_pairing {
namespace {

bool nonzero(const uint8_t* value, size_t size)
{
    return value != nullptr &&
           std::any_of(value, value + size, [](uint8_t byte) {
               return byte != 0;
           });
}

bool valid_utf8_name(const AgentName& name, size_t size)
{
    if (size == 0 || size > kAgentNameBytes || name[size] != '\0') {
        return false;
    }
    size_t index = 0;
    while (index < size) {
        const uint8_t first = static_cast<uint8_t>(name[index]);
        if (first < 0x20U || first == 0x7fU) {
            return false;
        }
        if (first < 0x80U) {
            ++index;
            continue;
        }
        size_t continuation = 0;
        uint32_t codepoint = 0;
        if ((first & 0xe0U) == 0xc0U) {
            continuation = 1;
            codepoint = first & 0x1fU;
        } else if ((first & 0xf0U) == 0xe0U) {
            continuation = 2;
            codepoint = first & 0x0fU;
        } else if ((first & 0xf8U) == 0xf0U) {
            continuation = 3;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (index + continuation >= size) {
            return false;
        }
        for (size_t offset = 1; offset <= continuation; ++offset) {
            const uint8_t next = static_cast<uint8_t>(name[index + offset]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        const bool overlong =
            (continuation == 1 && codepoint < 0x80U) ||
            (continuation == 2 && codepoint < 0x800U) ||
            (continuation == 3 && codepoint < 0x10000U);
        if (overlong || codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
            (codepoint >= 0x300U && codepoint <= 0x36fU)) {
            return false;
        }
        index += continuation + 1;
    }
    return true;
}

size_t name_size(const AgentName& name)
{
    const auto end = std::find(name.begin(), name.end(), '\0');
    return static_cast<size_t>(end - name.begin());
}

bool fingerprint(
    const IdentityPublicKey& public_key,
    AgentFingerprint& output)
{
    std::array<uint8_t, 32> digest = {};
    if (mbedtls_sha256(
            public_key.data(),
            public_key.size(),
            digest.data(),
            0) != 0) {
        return false;
    }
    std::copy_n(digest.begin(), output.size(), output.begin());
    return true;
}

bool fill_nonzero(
    RegistryEntropy entropy,
    void* context,
    uint8_t* output,
    size_t size)
{
    return entropy != nullptr && output != nullptr &&
           entropy(context, output, size) && nonzero(output, size);
}

bool valid_record(const AgentRecord& record)
{
    AgentFingerprint expected = {};
    const size_t size = name_size(record.display_name);
    return crypto::valid_public_key(record.public_key) &&
           fingerprint(record.public_key, expected) &&
           expected == record.fingerprint &&
           valid_utf8_name(record.display_name, size);
}

}  // namespace

RegistryResult AgentRegistry::initialize(
    RegistryEntropy entropy,
    void* entropy_context,
    AgentRegistry& output)
{
    output = {};
    RegistrySnapshot data = {};
    if (!fill_nonzero(
            entropy,
            entropy_context,
            data.watch_id.data(),
            data.watch_id.size()) ||
        !fill_nonzero(
            entropy,
            entropy_context,
            data.master_key.data(),
            data.master_key.size())) {
        return RegistryResult::EntropyFailure;
    }
    data.key_epoch = 1;
    output._data = data;
    output._initialized = true;
    return RegistryResult::Ok;
}

RegistryResult AgentRegistry::restore(
    const RegistrySnapshot& snapshot,
    AgentRegistry& output)
{
    output = {};
    if (snapshot.key_epoch == 0 || snapshot.count > kMaxAgents ||
        !nonzero(snapshot.watch_id.data(), snapshot.watch_id.size()) ||
        !nonzero(snapshot.master_key.data(), snapshot.master_key.size())) {
        return RegistryResult::InvalidRecord;
    }
    for (size_t index = 0; index < snapshot.count; ++index) {
        if (!valid_record(snapshot.records[index])) {
            return RegistryResult::InvalidRecord;
        }
        for (size_t prior = 0; prior < index; ++prior) {
            if (snapshot.records[index].fingerprint ==
                snapshot.records[prior].fingerprint) {
                return RegistryResult::InvalidRecord;
            }
        }
    }
    if (snapshot.has_last_selected) {
        const bool found = std::any_of(
            snapshot.records.begin(),
            snapshot.records.begin() +
                static_cast<std::ptrdiff_t>(snapshot.count),
            [&snapshot](const AgentRecord& record) {
                return record.fingerprint == snapshot.last_selected;
            });
        if (!found) {
            return RegistryResult::InvalidRecord;
        }
    }
    output._data = snapshot;
    output._initialized = true;
    return RegistryResult::Ok;
}

RegistryResult AgentRegistry::add(const AgentEnrollment& enrollment)
{
    if (!_initialized ||
        !valid_utf8_name(
            enrollment.display_name, enrollment.display_name_size)) {
        return RegistryResult::InvalidName;
    }
    if (!crypto::valid_public_key(enrollment.public_key)) {
        return RegistryResult::InvalidIdentity;
    }
    AgentFingerprint candidate = {};
    if (!fingerprint(enrollment.public_key, candidate)) {
        return RegistryResult::InvalidIdentity;
    }
    if (find(candidate) != nullptr) {
        return RegistryResult::Duplicate;
    }
    if (_data.count == kMaxAgents) {
        return RegistryResult::Full;
    }
    AgentRecord& record = _data.records[_data.count++];
    record.public_key = enrollment.public_key;
    record.fingerprint = candidate;
    record.display_name = enrollment.display_name;
    record.last_connected_at = enrollment.last_connected_at;
    return RegistryResult::Ok;
}

RegistryResult AgentRegistry::rename(
    const AgentFingerprint& fingerprint_value,
    const AgentName& display_name,
    size_t display_name_size)
{
    if (!valid_utf8_name(display_name, display_name_size)) {
        return RegistryResult::InvalidName;
    }
    AgentRecord* record = find(fingerprint_value);
    if (record == nullptr) {
        return RegistryResult::NotFound;
    }
    record->display_name = display_name;
    return RegistryResult::Ok;
}

RegistryResult AgentRegistry::select(
    const AgentFingerprint& fingerprint_value,
    int64_t connected_at)
{
    AgentRecord* record = find(fingerprint_value);
    if (record == nullptr) {
        return RegistryResult::NotFound;
    }
    record->last_connected_at = connected_at;
    _data.has_last_selected = true;
    _data.last_selected = fingerprint_value;
    return RegistryResult::Ok;
}

RegistryResult AgentRegistry::remove_and_rotate(
    const AgentFingerprint& fingerprint_value,
    RegistryEntropy entropy,
    void* entropy_context)
{
    size_t index = 0;
    while (index < _data.count &&
           _data.records[index].fingerprint != fingerprint_value) {
        ++index;
    }
    if (index == _data.count) {
        return RegistryResult::NotFound;
    }
    if (_data.key_epoch == std::numeric_limits<uint32_t>::max()) {
        return RegistryResult::EpochExhausted;
    }
    WatchMasterKey next_key = {};
    if (!fill_nonzero(
            entropy,
            entropy_context,
            next_key.data(),
            next_key.size())) {
        return RegistryResult::EntropyFailure;
    }
    for (size_t cursor = index + 1; cursor < _data.count; ++cursor) {
        _data.records[cursor - 1] = _data.records[cursor];
    }
    _data.records[--_data.count] = {};
    if (_data.has_last_selected &&
        _data.last_selected == fingerprint_value) {
        _data.has_last_selected = false;
        _data.last_selected = {};
    }
    std::fill(_data.master_key.begin(), _data.master_key.end(), uint8_t{0});
    _data.master_key = next_key;
    ++_data.key_epoch;
    return RegistryResult::Ok;
}

RegistryResult AgentRegistry::reset_all(
    RegistryEntropy entropy,
    void* entropy_context)
{
    if (!_initialized) {
        return RegistryResult::InvalidRecord;
    }
    if (_data.key_epoch == std::numeric_limits<uint32_t>::max()) {
        return RegistryResult::EpochExhausted;
    }
    WatchMasterKey next_key = {};
    if (!fill_nonzero(
            entropy,
            entropy_context,
            next_key.data(),
            next_key.size())) {
        return RegistryResult::EntropyFailure;
    }
    std::fill(_data.master_key.begin(), _data.master_key.end(), uint8_t{0});
    _data.master_key = next_key;
    ++_data.key_epoch;
    _data.records = {};
    _data.count = 0;
    _data.has_last_selected = false;
    _data.last_selected = {};
    return RegistryResult::Ok;
}

const AgentRecord* AgentRegistry::find(
    const AgentFingerprint& fingerprint_value) const
{
    for (size_t index = 0; index < _data.count; ++index) {
        if (_data.records[index].fingerprint == fingerprint_value) {
            return &_data.records[index];
        }
    }
    return nullptr;
}

AgentRecord* AgentRegistry::find(const AgentFingerprint& fingerprint_value)
{
    return const_cast<AgentRecord*>(
        static_cast<const AgentRegistry&>(*this).find(fingerprint_value));
}

const std::array<AgentRecord, kMaxAgents>& AgentRegistry::records() const
{
    return _data.records;
}

size_t AgentRegistry::count() const
{
    return _data.count;
}

const std::array<uint8_t, 16>& AgentRegistry::watch_id() const
{
    return _data.watch_id;
}

const WatchMasterKey& AgentRegistry::master_key() const
{
    return _data.master_key;
}

uint32_t AgentRegistry::key_epoch() const
{
    return _data.key_epoch;
}

bool AgentRegistry::has_last_selected() const
{
    return _data.has_last_selected;
}

const AgentFingerprint& AgentRegistry::last_selected() const
{
    return _data.last_selected;
}

bool AgentRegistry::initialized() const
{
    return _initialized;
}

RegistrySnapshot AgentRegistry::snapshot() const
{
    return _data;
}

bool operator==(const AgentRecord& lhs, const AgentRecord& rhs)
{
    return lhs.public_key == rhs.public_key &&
           lhs.fingerprint == rhs.fingerprint &&
           lhs.display_name == rhs.display_name &&
           lhs.last_connected_at == rhs.last_connected_at;
}

bool operator==(const AgentRegistry& lhs, const AgentRegistry& rhs)
{
    if (lhs.initialized() != rhs.initialized()) {
        return false;
    }
    if (!lhs.initialized()) {
        return true;
    }
    const RegistrySnapshot left = lhs.snapshot();
    const RegistrySnapshot right = rhs.snapshot();
    return left.watch_id == right.watch_id &&
           left.master_key == right.master_key &&
           left.key_epoch == right.key_epoch &&
           left.has_last_selected == right.has_last_selected &&
           left.last_selected == right.last_selected &&
           left.count == right.count &&
           std::equal(
               left.records.begin(),
               left.records.begin() +
                   static_cast<std::ptrdiff_t>(left.count),
               right.records.begin());
}

bool operator!=(const AgentRegistry& lhs, const AgentRegistry& rhs)
{
    return !(lhs == rhs);
}

}  // namespace secure_pairing
