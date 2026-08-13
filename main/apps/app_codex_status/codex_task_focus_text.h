#pragma once

#include <cstddef>
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

}  // namespace codex_task_focus_layout
