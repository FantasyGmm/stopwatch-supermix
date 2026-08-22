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
        result.connection = {
            "连接异常", palette.red, palette.red, true};
    } else if (codex_status_contains(base_text, "LIVE")) {
        result.connection = {
            "Mac 已连接", palette.mint, palette.soft, true};
    } else if (base_text != nullptr && base_text[0] != '\0' &&
               std::strcmp(base_text, "START") != 0) {
        result.connection = {
            "连接中", palette.violet, palette.soft, true};
    } else {
        result.connection = {
            "未连接", palette.dim, palette.dim, false};
    }

    if (voice_error) {
        result.voice = {"语音异常", palette.red, palette.red, true};
    } else if (voice_active) {
        result.voice = {"语音中", palette.cyan, palette.cyan, true};
    } else if (voice_starting) {
        result.voice = {
            "语音连接中", palette.violet, palette.soft, true};
    } else if (voice_ready) {
        result.voice = {"语音可用", palette.cyan, palette.soft, true};
    } else {
        result.voice = {"语音不可用", palette.dim, palette.dim, false};
    }
    return result;
}
