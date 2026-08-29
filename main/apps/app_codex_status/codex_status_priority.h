#pragma once

#include <cstdint>
#include <cstring>


struct CodexStatusPalette {
    uint32_t mint = 0;
    uint32_t cyan = 0;
    uint32_t violet = 0;
    uint32_t red = 0;
    uint32_t dim = 0;
    uint32_t soft = 0;
};


struct CodexChannelPresentation {
    const char* text = "";
    uint32_t dot_color = 0;
    uint32_t text_color = 0;
    bool filled = false;
};


struct CodexStatusChannels {
    CodexChannelPresentation connection;
    CodexChannelPresentation voice;
};


inline bool codex_status_contains(const char* value, const char* token)
{
    return value != nullptr && token != nullptr &&
        std::strstr(value, token) != nullptr;
}

inline const char* codex_task_model_display(const char* model)
{
    if (model == nullptr) {
        return "";
    }
    return std::strncmp(model, "gpt-", 4) == 0 ? model + 4 : model;
}


inline CodexStatusChannels resolve_codex_status_channels(
    const char* base_text,
    bool base_is_error,
    bool voice_ready,
    bool voice_starting,
    bool voice_active,
    bool voice_error,
    const CodexStatusPalette& palette)
{
    CodexStatusChannels result;
    if (base_is_error || codex_status_contains(base_text, "ERR")) {
        result.connection = {"ERROR", palette.red, palette.red, true};
    } else if (codex_status_contains(base_text, "LIVE")) {
        result.connection = {"CONNECTED", palette.mint, palette.soft, true};
    } else if (base_text != nullptr && base_text[0] != '\0' &&
               std::strcmp(base_text, "START") != 0) {
        result.connection = {"CONNECTING", palette.violet, palette.soft, true};
    } else {
        result.connection = {"UNAVAILABLE", palette.red, palette.red, true};
    }

    if (voice_error) {
        result.voice = {"ERROR", palette.red, palette.red, true};
    } else if (voice_active) {
        result.voice = {"ACTIVE", palette.cyan, palette.cyan, true};
    } else if (voice_starting) {
        result.voice = {"CONNECTING", palette.violet, palette.soft, true};
    } else if (voice_ready) {
        result.voice = {"READY", palette.mint, palette.soft, true};
    } else {
        result.voice = {"ERROR", palette.red, palette.red, true};
    }
    return result;
}
