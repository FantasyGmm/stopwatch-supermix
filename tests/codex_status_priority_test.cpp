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
    assert(std::string_view(channels.connection.text) == "CONNECTED");
    assert(channels.connection.dot_color == kPalette.mint);
    assert(channels.connection.text_color == kPalette.soft);
    assert(channels.connection.filled);
    assert(std::string_view(channels.voice.text) == "READY");
    assert(channels.voice.dot_color == kPalette.mint);
    assert(channels.voice.text_color == kPalette.soft);
    assert(channels.voice.filled);
}

void assert_connecting_and_starting_states_are_explicit()
{
    const auto channels = resolve_codex_status_channels(
        "PAIRING", false, false, true, false, false, kPalette);
    assert(std::string_view(channels.connection.text) == "CONNECTING");
    assert(channels.connection.dot_color == kPalette.violet);
    assert(channels.connection.filled);
    assert(std::string_view(channels.voice.text) == "CONNECTING");
    assert(channels.voice.dot_color == kPalette.violet);
    assert(channels.voice.filled);
}

void assert_disconnected_and_unavailable_voice_use_one_error_state()
{
    const auto channels = resolve_codex_status_channels(
        "", false, false, false, false, false, kPalette);
    assert(std::string_view(channels.connection.text) == "UNAVAILABLE");
    assert(channels.connection.text_color == kPalette.red);
    assert(std::string_view(channels.voice.text) == "ERROR");
    assert(channels.voice.dot_color == kPalette.red);
    assert(channels.voice.text_color == kPalette.red);
}

void assert_connection_error_does_not_hide_active_voice()
{
    const auto channels = resolve_codex_status_channels(
        "BLE ERR", true, false, false, true, false, kPalette);
    assert(std::string_view(channels.connection.text) == "ERROR");
    assert(channels.connection.dot_color == kPalette.red);
    assert(std::string_view(channels.voice.text) == "ACTIVE");
    assert(channels.voice.dot_color == kPalette.cyan);
}

void assert_voice_error_does_not_hide_live_connection()
{
    const auto channels = resolve_codex_status_channels(
        "LIVE", false, false, false, false, true, kPalette);
    assert(std::string_view(channels.connection.text) == "CONNECTED");
    assert(channels.connection.dot_color == kPalette.mint);
    assert(std::string_view(channels.voice.text) == "ERROR");
    assert(channels.voice.dot_color == kPalette.red);
    assert(channels.voice.text_color == kPalette.red);
}

void assert_task_model_display_only_strips_the_lowercase_gpt_prefix()
{
    assert(std::string_view(codex_task_model_display("gpt-5.6-sol")) == "5.6-sol");
    assert(std::string_view(codex_task_model_display("GPT-5.6-sol")) == "GPT-5.6-sol");
    assert(std::string_view(codex_task_model_display("custom")) == "custom");
    assert(std::string_view(codex_task_model_display(nullptr)).empty());
}

}  // namespace

int main()
{
    assert_live_connection_and_ready_voice_are_independent();
    assert_connecting_and_starting_states_are_explicit();
    assert_disconnected_and_unavailable_voice_use_one_error_state();
    assert_connection_error_does_not_hide_active_voice();
    assert_voice_error_does_not_hide_live_connection();
    assert_task_model_display_only_strips_the_lowercase_gpt_prefix();
}
