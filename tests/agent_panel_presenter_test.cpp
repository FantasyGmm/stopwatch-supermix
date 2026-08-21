#include "main/apps/app_codex_status/agent_panel_presenter.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

constexpr int64_t kNow = 1700000000000LL;

void expect_text(
    AgentPanelRowState state,
    int64_t last_connected_at,
    const char* expected)
{
    std::array<char, 16> text = {};
    agent_panel_presentation::format_last_connected(
        state,
        last_connected_at,
        kNow,
        text.data(),
        text.size());
    assert(std::strcmp(text.data(), expected) == 0);
}

}  // namespace

int main()
{
    assert(setenv("TZ", "UTC", 1) == 0);
    tzset();

    expect_text(AgentPanelRowState::Connected, 0, "Now");
    expect_text(AgentPanelRowState::Requesting, 0, "Awaiting");
    expect_text(AgentPanelRowState::Offline, 1234, "Unknown");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 8LL * 60LL * 1000LL,
        "8 min");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 59LL * 60LL * 1000LL,
        "59 min");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 60LL * 60LL * 1000LL,
        "1 hr");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 23LL * 60LL * 60LL * 1000LL,
        "23 hr");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 24LL * 60LL * 60LL * 1000LL,
        "Yesterday");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 47LL * 60LL * 60LL * 1000LL,
        "Yesterday");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 48LL * 60LL * 60LL * 1000LL,
        "Sun");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 6LL * 24LL * 60LL * 60LL * 1000LL,
        "Wed");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 7LL * 24LL * 60LL * 60LL * 1000LL,
        "7 days");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 29LL * 24LL * 60LL * 60LL * 1000LL,
        "29 days");
    expect_text(
        AgentPanelRowState::Offline,
        kNow - 30LL * 24LL * 60LL * 60LL * 1000LL,
        "10/15");
    expect_text(
        AgentPanelRowState::Offline,
        kNow + 1000LL,
        "Unknown");

    std::array<char, 16> invalid_now = {};
    agent_panel_presentation::format_last_connected(
        AgentPanelRowState::Offline,
        kNow,
        0,
        invalid_now.data(),
        invalid_now.size());
    assert(std::strcmp(invalid_now.data(), "Unknown") == 0);

    std::array<char, 1> tiny = {'x'};
    agent_panel_presentation::format_last_connected(
        AgentPanelRowState::Offline,
        kNow,
        kNow,
        tiny.data(),
        tiny.size());
    assert(tiny[0] == '\0');
}
