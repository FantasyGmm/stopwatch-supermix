#include "codex_status_codec.h"

#include <ArduinoJson.h>
#include <cJSON.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t kMaxDepth = 8;
constexpr size_t kMaxNodes = 256;
constexpr size_t kMaxStringBytes = 1024;
constexpr size_t kMaxKeyBytes = 64;
constexpr size_t kMaxDecodedStringBytes = 2048;
constexpr size_t kMaxDocumentBytes = 16 * 1024;
constexpr uint64_t kMaxExactInteger = 9007199254740991ULL;
constexpr size_t kMaxDetailCiphertextBytes = 1300 + 8;

// ArduinoJson may reserve a declared string length before reading it. Bound
// allocations as well as the input, so malformed bin32/str32 cannot reserve GBs.
class BoundedAllocator final : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override
    {
        if (size > kMaxDocumentBytes - _used) return nullptr;
        auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
        if (header == nullptr) return nullptr;
        header->size = size;
        _used += size;
        return header + 1;
    }

    void deallocate(void* pointer) override
    {
        if (pointer == nullptr) return;
        auto* header = static_cast<Header*>(pointer) - 1;
        _used -= header->size;
        std::free(header);
    }

    void* reallocate(void* pointer, size_t size) override
    {
        if (pointer == nullptr) return allocate(size);
        if (size == 0) {
            deallocate(pointer);
            return nullptr;
        }
        auto* header = static_cast<Header*>(pointer) - 1;
        const size_t remaining = _used - header->size;
        if (size > kMaxDocumentBytes - remaining) return nullptr;
        auto* resized = static_cast<Header*>(std::realloc(header, sizeof(Header) + size));
        if (resized == nullptr) return nullptr;
        resized->size = size;
        _used = remaining + size;
        return resized + 1;
    }

private:
    struct alignas(std::max_align_t) Header { size_t size; };
    size_t _used = 0;
};

class BoundedReader {
public:
    BoundedReader(const char* data, size_t length) : _data(data), _length(length) {}
    int read()
    {
        return _position < _length
            ? static_cast<unsigned char>(_data[_position++]) : -1;
    }
    size_t readBytes(char* output, size_t length)
    {
        const size_t count = std::min(length, _length - _position);
        std::memcpy(output, _data + _position, count);
        _position += count;
        return count;
    }
    size_t consumed() const { return _position; }

private:
    const char* _data;
    size_t _length;
    size_t _position = 0;
};

struct AdapterBudget {
    size_t nodes = 0;
    size_t strings = 0;
};

bool valid_utf8(const char* text, size_t length)
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(text);
    for (size_t offset = 0; offset < length;) {
        const uint8_t first = bytes[offset++];
        if (first < 0x80) continue;
        size_t continuation = 0;
        uint32_t codepoint = 0;
        if ((first & 0xe0) == 0xc0) {
            continuation = 1;
            codepoint = first & 0x1f;
        } else if ((first & 0xf0) == 0xe0) {
            continuation = 2;
            codepoint = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            continuation = 3;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if (continuation > length - offset) return false;
        for (size_t index = 0; index < continuation; ++index) {
            const uint8_t next = bytes[offset++];
            if ((next & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3f);
        }
        if ((continuation == 1 && codepoint < 0x80) ||
            (continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

bool bounded_string(ArduinoJson::JsonString text, size_t limit, AdapterBudget& budget)
{
    if (text.c_str() == nullptr || text.size() > limit ||
        text.size() > kMaxDecodedStringBytes - budget.strings ||
        std::memchr(text.c_str(), '\0', text.size()) != nullptr ||
        !valid_utf8(text.c_str(), text.size())) {
        return false;
    }
    budget.strings += text.size();
    return true;
}

CodexStatusObject adapt_object(
    ArduinoJson::JsonVariantConst value,
    size_t depth,
    AdapterBudget& budget,
    bool native_detail,
    bool allow_binary = false)
{
    if (depth > kMaxDepth || ++budget.nodes > kMaxNodes) return {};
    if (value.is<ArduinoJson::MsgPackBinary>()) {
        return CodexStatusObject(allow_binary ? cJSON_CreateNull() : nullptr);
    }
    if (value.isNull()) return CodexStatusObject(cJSON_CreateNull());
    if (value.is<bool>()) return CodexStatusObject(cJSON_CreateBool(value.as<bool>()));
    if (value.is<ArduinoJson::JsonObjectConst>()) {
        CodexStatusObject object(cJSON_CreateObject());
        if (!object) return {};
        for (ArduinoJson::JsonPairConst pair : value.as<ArduinoJson::JsonObjectConst>()) {
            const auto key = pair.key();
            if (!bounded_string(key, kMaxKeyBytes, budget) ||
                cJSON_GetObjectItemCaseSensitive(object.get(), key.c_str()) != nullptr) {
                return {};
            }
            const bool binary_field = depth == 0 && native_detail &&
                (std::strcmp(key.c_str(), "nonce") == 0 ||
                 std::strcmp(key.c_str(), "ciphertext") == 0);
            auto child = adapt_object(pair.value(), depth + 1, budget, false, binary_field);
            if (!child || !cJSON_AddItemToObject(object.get(), key.c_str(), child.get())) return {};
            child.release();
        }
        return object;
    }
    if (value.is<ArduinoJson::JsonArrayConst>()) {
        CodexStatusObject array(cJSON_CreateArray());
        if (!array) return {};
        for (ArduinoJson::JsonVariantConst item : value.as<ArduinoJson::JsonArrayConst>()) {
            auto child = adapt_object(item, depth + 1, budget, false);
            if (!child || !cJSON_AddItemToArray(array.get(), child.get())) return {};
            child.release();
        }
        return array;
    }
    if (value.is<ArduinoJson::JsonString>()) {
        const auto text = value.as<ArduinoJson::JsonString>();
        if (!bounded_string(text, kMaxStringBytes, budget)) return {};
        return CodexStatusObject(cJSON_CreateString(text.c_str()));
    }
    // Check native integers before converting to cJSON's double. Values beyond
    // the existing exact JSON bound are rejected, never rounded into range.
    double number = 0;
    if (value.is<uint64_t>()) {
        const uint64_t integer = value.as<uint64_t>();
        if (integer > kMaxExactInteger) return {};
        number = static_cast<double>(integer);
    } else if (value.is<int64_t>()) {
        const int64_t integer = value.as<int64_t>();
        if (integer < -static_cast<int64_t>(kMaxExactInteger)) return {};
        number = static_cast<double>(integer);
    } else if (value.is<double>()) {
        number = value.as<double>();
        if (!std::isfinite(number) || std::abs(number) > kMaxExactInteger) return {};
    } else {
        // Extensions are retained as raw values by ArduinoJson, not JSON values.
        return {};
    }
    return CodexStatusObject(cJSON_CreateNumber(number));
}

}  // namespace

void CodexStatusObjectDeleter::operator()(cJSON* root) const
{
    cJSON_Delete(root);
}

bool decode_codex_status_payload(
    const char* data,
    size_t length,
    CodexStatusEncoding encoding,
    CodexDecodedStatusPayload& output)
{
    output = {};
    if (data == nullptr || length == 0 || length > kCodexStatusMaxPayloadBytes) return false;
    CodexDecodedStatusPayload decoded;
    switch (encoding) {
        case CodexStatusEncoding::Json: {
            const char* end = nullptr;
            decoded.root.reset(cJSON_ParseWithLengthOpts(data, length, &end, 0));
            if (!decoded.root || !cJSON_IsObject(decoded.root.get()) ||
                end == nullptr || end < data || end > data + length) return false;
            while (end < data + length) {
                if (*end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') return false;
                ++end;
            }
            break;
        }
        case CodexStatusEncoding::MessagePack: {
            BoundedAllocator allocator;
            ArduinoJson::JsonDocument document(&allocator);
            BoundedReader reader(data, length);
            if (ArduinoJson::deserializeMsgPack(
                    document, reader, ArduinoJson::DeserializationOption::NestingLimit(kMaxDepth)) ||
                reader.consumed() != length ||
                !document.is<ArduinoJson::JsonObjectConst>()) return false;
            const auto root = document.as<ArduinoJson::JsonObjectConst>();
            const auto type = root["type"];
            decoded.has_native_detail = type.is<const char*>() &&
                std::strcmp(type.as<const char*>(), "task_detail") == 0;
            if (decoded.has_native_detail) {
                if (!root["nonce"].is<ArduinoJson::MsgPackBinary>() ||
                    !root["ciphertext"].is<ArduinoJson::MsgPackBinary>()) return false;
                const auto nonce = root["nonce"].as<ArduinoJson::MsgPackBinary>();
                const auto ciphertext = root["ciphertext"].as<ArduinoJson::MsgPackBinary>();
                if (nonce.size() != decoded.native_detail.exchange_nonce.size() ||
                    ciphertext.size() < 8 || ciphertext.size() > kMaxDetailCiphertextBytes) return false;
                std::memcpy(decoded.native_detail.exchange_nonce.data(), nonce.data(), nonce.size());
                const auto* bytes = static_cast<const uint8_t*>(ciphertext.data());
                decoded.native_detail.ciphertext.assign(bytes, bytes + ciphertext.size());
            }
            AdapterBudget budget;
            decoded.root = adapt_object(document.as<ArduinoJson::JsonVariantConst>(), 0,
                                        budget, decoded.has_native_detail);
            if (!decoded.root) return false;
            break;
        }
        default:
            return false;
    }
    output = std::move(decoded);
    return true;
}
