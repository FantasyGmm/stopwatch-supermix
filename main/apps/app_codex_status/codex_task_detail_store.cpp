#include "codex_task_detail_store.h"

#include <algorithm>

namespace {

constexpr size_t kMaxRoutes = 8;
constexpr size_t kMaxChunks = 16;
constexpr size_t kMaxChunkBytes = 1300;
constexpr size_t kMaxReplyBytes = 12 * 1024;
constexpr size_t kMaxSeenNonces = 64;
const std::string kTruncationMarker = "\n\n[内容过长，手表仅显示前 12 KiB]";

bool nonzero(const voice_input::crypto::ExchangeNonce& nonce)
{
    return std::any_of(nonce.begin(), nonce.end(), [](uint8_t byte) {
        return byte != 0;
    });
}

bool valid_utf8(const std::string& text)
{
    size_t offset = 0;
    while (offset < text.size()) {
        const uint8_t first = static_cast<uint8_t>(text[offset]);
        if (first < 0x80U) {
            ++offset;
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
        if (offset + continuation >= text.size()) {
            return false;
        }
        for (size_t index = 1; index <= continuation; ++index) {
            const uint8_t next = static_cast<uint8_t>(text[offset + index]);
            if ((next & 0xc0U) != 0x80U) {
                return false;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 1 && codepoint < 0x80U) ||
            (continuation == 2 && codepoint < 0x800U) ||
            (continuation == 3 && codepoint < 0x10000U) ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU)) {
            return false;
        }
        offset += continuation + 1;
    }
    return true;
}

bool valid_complete_text(const std::string& text)
{
    if (text.size() <= kMaxReplyBytes) {
        return true;
    }
    if (text.size() > kMaxReplyBytes + kTruncationMarker.size() ||
        text.size() < kTruncationMarker.size()) {
        return false;
    }
    const size_t marker_offset = text.size() - kTruncationMarker.size();
    return marker_offset <= kMaxReplyBytes &&
           text.compare(marker_offset, kTruncationMarker.size(), kTruncationMarker) == 0;
}

void clear_string(std::string& value)
{
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
}

}  // namespace

bool CodexTaskDetailStore::accept_sequence(
    const voice_input::crypto::ExchangeNonce& nonce,
    uint32_t sequence)
{
    if (!nonzero(nonce) || sequence == 0) {
        return false;
    }
    if (_has_active_nonce && nonce == _active_nonce) {
        if (sequence <= _highest_sequence || _highest_sequence == UINT32_MAX) {
            return false;
        }
        _highest_sequence = sequence;
        return true;
    }
    if (std::find(_seen_nonces.begin(), _seen_nonces.begin() + _seen_nonce_count, nonce) !=
        _seen_nonces.begin() + _seen_nonce_count) {
        return false;
    }
    if (_seen_nonce_count >= kMaxSeenNonces) {
        return false;
    }
    _seen_nonces[_seen_nonce_count++] = nonce;
    _active_nonce = nonce;
    _has_active_nonce = true;
    _highest_sequence = sequence;
    return true;
}

void CodexTaskDetailStore::clear_content()
{
    for (auto& item : _pending) {
        for (std::string& part : item.second.chunks) {
            clear_string(part);
        }
    }
    for (auto& item : _published) {
        clear_string(item.second.text);
    }
    _pending.clear();
    _published.clear();
}

bool CodexTaskDetailStore::accept(const CodexTaskDetailChunk& chunk)
{
    if (_key_epoch == 0 || chunk.route == 0 || chunk.revision < 0 ||
        chunk.count == 0 || chunk.count > kMaxChunks || chunk.index >= chunk.count ||
        chunk.text.size() > kMaxChunkBytes || !valid_utf8(chunk.text) ||
        !accept_sequence(chunk.exchange_nonce, chunk.sequence)) {
        return false;
    }

    const auto published = _published.find(chunk.route);
    if (published != _published.end()) {
        if (chunk.revision < published->second.revision) {
            return false;
        }
        if (chunk.revision == published->second.revision) {
            return chunk.count == 1 && chunk.index == 0 &&
                   chunk.text == published->second.text;
        }
    }
    auto pending = _pending.find(chunk.route);
    if (pending != _pending.end() && chunk.revision < pending->second.revision) {
        return false;
    }
    if (pending == _pending.end() || chunk.revision > pending->second.revision) {
        size_t route_count = _published.size();
        for (const auto& item : _pending) {
            if (_published.find(item.first) == _published.end()) {
                ++route_count;
            }
        }
        if (pending == _pending.end() && published == _published.end() &&
            route_count >= kMaxRoutes) {
            return false;
        }
        Pending replacement;
        replacement.revision = chunk.revision;
        replacement.count = chunk.count;
        replacement.chunks.resize(chunk.count);
        replacement.received.assign(chunk.count, false);
        pending = _pending.insert_or_assign(chunk.route, std::move(replacement)).first;
    }
    Pending& value = pending->second;
    if (value.count != chunk.count) {
        _pending.erase(pending);
        return false;
    }
    if (value.received[chunk.index]) {
        if (value.chunks[chunk.index] == chunk.text) {
            return true;
        }
        _pending.erase(pending);
        return false;
    }
    value.chunks[chunk.index] = chunk.text;
    value.received[chunk.index] = true;
    if (!std::all_of(value.received.begin(), value.received.end(), [](bool present) {
            return present;
        })) {
        return true;
    }
    std::string assembled;
    for (const std::string& part : value.chunks) {
        assembled += part;
        if (assembled.size() > kMaxReplyBytes + kTruncationMarker.size()) {
            _pending.erase(pending);
            return false;
        }
    }
    if (!valid_complete_text(assembled)) {
        _pending.erase(pending);
        return false;
    }
    _published[chunk.route] = {chunk.revision, std::move(assembled)};
    _pending.erase(pending);
    return true;
}

const CodexTaskDetail* CodexTaskDetailStore::find(uint64_t route) const
{
    const auto found = _published.find(route);
    return found == _published.end() ? nullptr : &found->second;
}

void CodexTaskDetailStore::retain_routes(const std::vector<uint64_t>& routes)
{
    const auto retained = [&routes](uint64_t route) {
        return std::find(routes.begin(), routes.end(), route) != routes.end();
    };
    for (auto iterator = _pending.begin(); iterator != _pending.end();) {
        iterator = retained(iterator->first) ? std::next(iterator) : _pending.erase(iterator);
    }
    for (auto iterator = _published.begin(); iterator != _published.end();) {
        if (retained(iterator->first)) {
            ++iterator;
        } else {
            clear_string(iterator->second.text);
            iterator = _published.erase(iterator);
        }
    }
}

void CodexTaskDetailStore::reset_for_credential(uint32_t key_epoch)
{
    if (key_epoch == 0) {
        reset();
        return;
    }
    if (_key_epoch != 0 && key_epoch == _key_epoch) {
        clear_content();
        _has_active_nonce = false;
        _active_nonce = {};
        _highest_sequence = 0;
        return;
    }
    reset();
    _key_epoch = key_epoch;
}

void CodexTaskDetailStore::reset_connection()
{
    for (auto& item : _pending) {
        for (std::string& part : item.second.chunks) {
            clear_string(part);
        }
    }
    _pending.clear();
    _has_active_nonce = false;
    _active_nonce = {};
    _highest_sequence = 0;
}

void CodexTaskDetailStore::reset()
{
    clear_content();
    _key_epoch = 0;
    _has_active_nonce = false;
    _active_nonce = {};
    _highest_sequence = 0;
    _seen_nonces = {};
    _seen_nonce_count = 0;
}
