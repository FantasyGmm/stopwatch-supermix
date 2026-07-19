#include "debug/debug_command_parser.h"
#include "debug/debug_app_control.h"

#include <cassert>
#include <string>
#include <string_view>

namespace {
class FakeDebugAppTarget final : public DebugAppTarget {
public:
    DebugAppOpenResult openDebugApp(std::string_view name) override
    {
        last_name = std::string(name);
        return next_result;
    }

    void logDebugAppList() override
    {
        ++list_calls;
    }

    bool goDebugHome() override
    {
        ++home_calls;
        return home_result;
    }

    DebugAppOpenResult next_result = DebugAppOpenResult::NotFound;
    std::string last_name;
    int list_calls = 0;
    int home_calls = 0;
    bool home_result = true;
};
} // namespace

int main()
{
    const auto list = ParseDebugCommand("debug app list");
    assert(list.kind == DebugCommandKind::AppList);
    assert(list.error == DebugCommandError::None);

    const auto open = ParseDebugCommand("debug app open AsSiStAnT\r");
    assert(open.kind == DebugCommandKind::AppOpen);
    assert(open.error == DebugCommandError::None);
    assert(std::string_view(open.argument.data()) == "assistant");

    const auto home = ParseDebugCommand("debug app home");
    assert(home.kind == DebugCommandKind::AppHome);
    assert(home.error == DebugCommandError::None);

    assert(ParseDebugCommand("app open assistant").error ==
           DebugCommandError::InvalidCommand);
    assert(ParseDebugCommand("debug app open").error ==
           DebugCommandError::InvalidCommand);
    assert(ParseDebugCommand("debug app open assistant now").error ==
           DebugCommandError::InvalidCommand);
    assert(ParseDebugCommand(std::string(65, 'x')).error ==
           DebugCommandError::LineTooLong);

    assert(DebugAppNameEquals("Assistant", "assistant"));
    assert(DebugAppNameEquals("Codex Status", "codex-status"));
    assert(DebugAppNameEquals("Audio.FFT", "audio-fft"));
    assert(!DebugAppNameEquals("Assistant", "codex"));

    FakeDebugAppTarget target;
    assert(RequestDebugAppOpen("assistant") == DebugAppOpenResult::Busy);
    assert(!RequestDebugAppList());
    assert(RegisterDebugAppTarget(&target));
    assert(!RegisterDebugAppTarget(&target));
    target.next_result = DebugAppOpenResult::Ok;
    assert(RequestDebugAppOpen("assistant") == DebugAppOpenResult::Ok);
    assert(target.last_name == "assistant");
    assert(RequestDebugAppList());
    assert(target.list_calls == 1);
    assert(RequestDebugAppHome());
    assert(target.home_calls == 1);
    UnregisterDebugAppTarget(&target);
    assert(RequestDebugAppOpen("assistant") == DebugAppOpenResult::Busy);
    assert(!RequestDebugAppHome());
    return 0;
}
