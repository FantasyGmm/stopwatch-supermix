#pragma once

#include <cstddef>
#include <cstdint>
#include <string>


namespace codex_task_focus_layout {

inline size_t utf8_scalar_count(const std::string& text)
{
    size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}


inline std::string elide_utf8(const char* value, size_t maximum_scalars)
{
    if (value == nullptr || maximum_scalars == 0) {
        return {};
    }
    const std::string text(value);
    if (utf8_scalar_count(text) <= maximum_scalars) {
        return text;
    }
    if (maximum_scalars == 1) {
        return "…";
    }

    size_t bytes = 0;
    size_t scalars = 0;
    while (bytes < text.size() && scalars < maximum_scalars - 1) {
        ++bytes;
        while (bytes < text.size() &&
               (static_cast<unsigned char>(text[bytes]) & 0xC0U) == 0x80U) {
            ++bytes;
        }
        ++scalars;
    }
    return text.substr(0, bytes) + "…";
}


inline std::string detail_display_text(const char* value)
{
    if (value == nullptr) {
        return {};
    }
    const std::string source(value);
    std::string output;
    output.reserve(source.size());
    size_t index = 0;
    while (index < source.size()) {
        const bool image = source[index] == '!' &&
            index + 1 < source.size() && source[index + 1] == '[';
        const size_t label_open = image ? index + 1 : index;
        if (source[label_open] != '[') {
            output.push_back(source[index++]);
            continue;
        }
        const size_t label_close = source.find(']', label_open + 1);
        if (label_close == std::string::npos ||
            label_close + 1 >= source.size() ||
            source[label_close + 1] != '(') {
            output.push_back(source[index++]);
            continue;
        }
        size_t destination_end = label_close + 2;
        size_t depth = 1;
        for (; destination_end < source.size(); ++destination_end) {
            if (source[destination_end] == '\\' &&
                destination_end + 1 < source.size()) {
                ++destination_end;
                continue;
            }
            if (source[destination_end] == '(') {
                ++depth;
            } else if (source[destination_end] == ')' && --depth == 0) {
                break;
            }
        }
        if (destination_end == source.size()) {
            output.push_back(source[index++]);
            continue;
        }
        output.append(
            source, label_open + 1, label_close - label_open - 1);
        index = destination_end + 1;
    }
    return output;
}


class TaskDetailScrollIdentity {
public:
    bool update(uint64_t route, int32_t revision)
    {
        const bool changed = !_initialized || route != _route ||
            revision != _revision;
        _initialized = true;
        _route = route;
        _revision = revision;
        return changed;
    }

    void reset()
    {
        _initialized = false;
        _route = 0;
        _revision = -1;
    }

private:
    bool _initialized = false;
    uint64_t _route = 0;
    int32_t _revision = -1;
};

}  // namespace codex_task_focus_layout
