#include "secure_pairing_protocol.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace secure_pairing {
namespace {

constexpr std::array<uint8_t, 6> kMagic = {'S', 'W', 'P', 'A', 'I', 'R'};

bool nonzero(const RandomValue& value)
{
    return std::any_of(value.begin(), value.end(), [](uint8_t byte) {
        return byte != 0;
    });
}

bool valid_utf8_name(
    const std::array<char, kMaxAgentNameBytes>& name,
    size_t size)
{
    if (size == 0 || size > name.size()) {
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

bool valid(const PairingTranscript& value)
{
    return nonzero(value.watch_nonce) && nonzero(value.agent_nonce) &&
           crypto::valid_public_key(value.watch_ephemeral_public) &&
           crypto::valid_public_key(value.agent_ephemeral_public) &&
           crypto::valid_public_key(value.agent_identity_public) &&
           valid_utf8_name(value.agent_name, value.agent_name_size);
}

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

    bool u16(size_t value)
    {
        if (value > 0xffffU) {
            return false;
        }
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

    bool field(const uint8_t* value, size_t size)
    {
        return u16(size) && bytes(value, size);
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

    bool byte(uint8_t& output)
    {
        return bytes(&output, 1);
    }

    bool u16(size_t& output)
    {
        std::array<uint8_t, 2> encoded = {};
        if (!bytes(encoded.data(), encoded.size())) {
            return false;
        }
        output = (static_cast<size_t>(encoded[0]) << 8U) | encoded[1];
        return true;
    }

    bool u32(uint32_t& output)
    {
        std::array<uint8_t, 4> encoded = {};
        if (!bytes(encoded.data(), encoded.size())) {
            return false;
        }
        output = (static_cast<uint32_t>(encoded[0]) << 24U) |
                 (static_cast<uint32_t>(encoded[1]) << 16U) |
                 (static_cast<uint32_t>(encoded[2]) << 8U) | encoded[3];
        return true;
    }

    bool field(uint8_t* output, size_t expected_size)
    {
        size_t size = 0;
        return u16(size) && size == expected_size && bytes(output, size);
    }

    bool name(PairingTranscript& output)
    {
        size_t size = 0;
        if (!u16(size) || size > output.agent_name.size()) {
            return false;
        }
        output.agent_name_size = size;
        return bytes(
            reinterpret_cast<uint8_t*>(output.agent_name.data()), size);
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

}  // namespace

bool operator==(const PairingTranscript& lhs, const PairingTranscript& rhs)
{
    return lhs.transaction_id == rhs.transaction_id &&
           lhs.watch_id == rhs.watch_id &&
           lhs.watch_nonce == rhs.watch_nonce &&
           lhs.agent_nonce == rhs.agent_nonce &&
           lhs.watch_ephemeral_public == rhs.watch_ephemeral_public &&
           lhs.agent_ephemeral_public == rhs.agent_ephemeral_public &&
           lhs.agent_identity_public == rhs.agent_identity_public &&
           lhs.agent_name_size == rhs.agent_name_size &&
           std::equal(
               lhs.agent_name.begin(),
               lhs.agent_name.begin() +
                   static_cast<std::ptrdiff_t>(lhs.agent_name_size),
               rhs.agent_name.begin());
}

bool operator!=(const PairingTranscript& lhs, const PairingTranscript& rhs)
{
    return !(lhs == rhs);
}

bool encode_pairing_transcript(
    const PairingTranscript& value,
    uint8_t* output,
    size_t output_capacity,
    size_t& output_size)
{
    output_size = 0;
    if (output == nullptr || !valid(value)) {
        return false;
    }
    Writer writer(output, output_capacity);
    if (!writer.bytes(kMagic.data(), kMagic.size()) ||
        !writer.byte(kPairingVersion) || !writer.u32(value.transaction_id) ||
        !writer.field(value.watch_id.data(), value.watch_id.size()) ||
        !writer.field(value.watch_nonce.data(), value.watch_nonce.size()) ||
        !writer.field(value.agent_nonce.data(), value.agent_nonce.size()) ||
        !writer.field(
            value.watch_ephemeral_public.data(),
            value.watch_ephemeral_public.size()) ||
        !writer.field(
            value.agent_ephemeral_public.data(),
            value.agent_ephemeral_public.size()) ||
        !writer.field(
            value.agent_identity_public.data(),
            value.agent_identity_public.size()) ||
        !writer.field(
            reinterpret_cast<const uint8_t*>(value.agent_name.data()),
            value.agent_name_size)) {
        return false;
    }
    output_size = writer.size();
    return true;
}

bool decode_pairing_transcript(
    const uint8_t* input,
    size_t input_size,
    PairingTranscript& output)
{
    output = {};
    if (input == nullptr || input_size > kMaxTranscriptSize) {
        return false;
    }
    Reader reader(input, input_size);
    std::array<uint8_t, kMagic.size()> magic = {};
    uint8_t version = 0;
    if (!reader.bytes(magic.data(), magic.size()) || magic != kMagic ||
        !reader.byte(version) || version != kPairingVersion ||
        !reader.u32(output.transaction_id) ||
        !reader.field(output.watch_id.data(), output.watch_id.size()) ||
        !reader.field(output.watch_nonce.data(), output.watch_nonce.size()) ||
        !reader.field(output.agent_nonce.data(), output.agent_nonce.size()) ||
        !reader.field(
            output.watch_ephemeral_public.data(),
            output.watch_ephemeral_public.size()) ||
        !reader.field(
            output.agent_ephemeral_public.data(),
            output.agent_ephemeral_public.size()) ||
        !reader.field(
            output.agent_identity_public.data(),
            output.agent_identity_public.size()) ||
        !reader.name(output) || !reader.done() || !valid(output)) {
        output = {};
        return false;
    }
    std::array<uint8_t, kMaxTranscriptSize> canonical = {};
    size_t canonical_size = 0;
    if (!encode_pairing_transcript(
            output, canonical.data(), canonical.size(), canonical_size) ||
        canonical_size != input_size ||
        !std::equal(canonical.begin(), canonical.begin() + canonical_size, input)) {
        output = {};
        return false;
    }
    return true;
}

}  // namespace secure_pairing
