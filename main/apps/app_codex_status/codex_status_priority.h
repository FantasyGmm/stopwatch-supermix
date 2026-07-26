#pragma once

#include <cstdint>

struct CodexStatusPresentation {
    const char* text = "";
    uint32_t color = 0;
};

inline CodexStatusPresentation resolve_codex_status(
    const char* base_text,
    uint32_t base_color,
    bool base_is_error,
    bool voice_active,
    bool voice_error,
    uint32_t voice_color,
    uint32_t error_color,
    uint32_t dim_color)
{
    if (base_is_error) {
        return {base_text, base_color};
    }
    if (voice_error) {
        return {"VOICE ERR", error_color};
    }
    if (voice_active) {
        return {"VOICE", voice_color};
    }
    if (base_text != nullptr && base_text[0] != '\0') {
        return {base_text, base_color};
    }
    return {"VOICE OFF", dim_color};
}
