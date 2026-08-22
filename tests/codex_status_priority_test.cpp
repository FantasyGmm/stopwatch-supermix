#include "main/apps/app_codex_status/codex_status_priority.h"

#include <cassert>
#include <string_view>

namespace {

constexpr CodexStatusPalette kPalette{
    0x53D7A4, 0x49C9EE, 0x7C9DFF, 0xFF6248, 0x788391, 0xC8D2DC,
};

void assert_live_connection_and_ready_voice_are_independent()
{
    const auto channels = resolve_codex_status_channels(
        "BLE LIVE", false, true, false, false, false, kPalette);
    assert(std::string_view(channels.connection.text) == "Mac 已连接");
    assert(channels.connection.dot_color == kPalette.mint);
    assert(channels.connection.text_color == kPalette.soft);
    assert(channels.connection.filled);
    assert(std::string_view(channels.voice.text) == "语音可用");
    assert(channels.voice.dot_color == kPalette.cyan);
    assert(channels.voice.text_color == kPalette.soft);
    assert(channels.voice.filled);
}

void assert_connecting_and_starting_states_are_explicit()
{
    const auto channels = resolve_codex_status_channels(
        "PAIRING", false, false, true, false, false, kPalette);
    assert(std::string_view(channels.connection.text) == "连接中");
    assert(channels.connection.dot_color == kPalette.violet);
    assert(channels.connection.filled);
    assert(std::string_view(channels.voice.text) == "语音连接中");
    assert(channels.voice.dot_color == kPalette.violet);
    assert(channels.voice.filled);
}

void assert_disconnected_and_unavailable_states_use_hollow_dots()
{
    const auto channels = resolve_codex_status_channels(
        "", false, false, false, false, false, kPalette);
    assert(std::string_view(channels.connection.text) == "未连接");
    assert(channels.connection.dot_color == kPalette.dim);
    assert(channels.connection.text_color == kPalette.dim);
    assert(!channels.connection.filled);
    assert(std::string_view(channels.voice.text) == "语音不可用");
    assert(channels.voice.dot_color == kPalette.dim);
    assert(!channels.voice.filled);
}

void assert_connection_error_does_not_hide_active_voice()
{
    const auto channels = resolve_codex_status_channels(
        "BLE ERR", true, false, false, true, false, kPalette);
    assert(std::string_view(channels.connection.text) == "连接异常");
    assert(channels.connection.dot_color == kPalette.red);
    assert(std::string_view(channels.voice.text) == "语音中");
    assert(channels.voice.dot_color == kPalette.cyan);
}

void assert_voice_error_does_not_hide_live_connection()
{
    const auto channels = resolve_codex_status_channels(
        "LIVE", false, false, false, false, true, kPalette);
    assert(std::string_view(channels.connection.text) == "Mac 已连接");
    assert(channels.connection.dot_color == kPalette.mint);
    assert(std::string_view(channels.voice.text) == "语音异常");
    assert(channels.voice.dot_color == kPalette.red);
    assert(channels.voice.text_color == kPalette.red);
}

}  // namespace

int main()
{
    assert_live_connection_and_ready_voice_are_independent();
    assert_connecting_and_starting_states_are_explicit();
    assert_disconnected_and_unavailable_states_use_hollow_dots();
    assert_connection_error_does_not_hide_active_voice();
    assert_voice_error_does_not_hide_live_connection();
}
