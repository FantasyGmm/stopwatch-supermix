#include "main/apps/app_codex_status/codex_runtime_session.h"

#include <cassert>
#include <vector>

namespace {

enum class CleanupEvent {
    DisconnectInputActions,
    DisconnectVoice,
    DisconnectVoiceProbe,
    ShutdownVoice,
    ShutdownVoiceProbe,
    StopTransport,
    ReleaseResources,
};

struct RecordingCleanup {
    void disconnect_input_actions()
    {
        events.push_back(CleanupEvent::DisconnectInputActions);
    }
    void disconnect_voice()
    {
        events.push_back(CleanupEvent::DisconnectVoice);
    }
    void disconnect_voice_probe()
    {
        events.push_back(CleanupEvent::DisconnectVoiceProbe);
    }
    void shutdown_voice()
    {
        events.push_back(CleanupEvent::ShutdownVoice);
    }
    void shutdown_voice_probe()
    {
        events.push_back(CleanupEvent::ShutdownVoiceProbe);
    }
    void stop_transport()
    {
        events.push_back(CleanupEvent::StopTransport);
    }
    void release_resources()
    {
        events.push_back(CleanupEvent::ReleaseResources);
    }

    std::vector<CleanupEvent> events;
};

void test_disconnect_and_shutdown_are_idempotent()
{
    CodexRuntimeSessionLifecycle lifecycle;
    RecordingCleanup cleanup;

    lifecycle.begin();
    assert(
        lifecycle.disconnect(cleanup) ==
        CodexRuntimeCleanupAction::Disconnect);
    assert((cleanup.events == std::vector<CleanupEvent>{
        CleanupEvent::DisconnectInputActions,
        CleanupEvent::DisconnectVoice,
        CleanupEvent::DisconnectVoiceProbe,
    }));
    assert(
        lifecycle.disconnect(cleanup) == CodexRuntimeCleanupAction::None);
    assert(
        lifecycle.shutdown(cleanup) == CodexRuntimeCleanupAction::Shutdown);
    assert((cleanup.events == std::vector<CleanupEvent>{
        CleanupEvent::DisconnectInputActions,
        CleanupEvent::DisconnectVoice,
        CleanupEvent::DisconnectVoiceProbe,
        CleanupEvent::DisconnectInputActions,
        CleanupEvent::ShutdownVoice,
        CleanupEvent::ShutdownVoiceProbe,
        CleanupEvent::StopTransport,
        CleanupEvent::ReleaseResources,
    }));
    assert(
        lifecycle.shutdown(cleanup) == CodexRuntimeCleanupAction::None);
    assert(cleanup.events.size() == 8);
}

void test_new_connection_rearms_disconnect_cleanup()
{
    CodexRuntimeSessionLifecycle lifecycle;
    RecordingCleanup cleanup;

    lifecycle.begin();
    assert(
        lifecycle.disconnect(cleanup) ==
        CodexRuntimeCleanupAction::Disconnect);
    lifecycle.begin();
    assert(
        lifecycle.disconnect(cleanup) ==
        CodexRuntimeCleanupAction::Disconnect);
    assert(cleanup.events.size() == 6);
}

void test_shutdown_covers_an_active_connection()
{
    CodexRuntimeSessionLifecycle lifecycle;
    RecordingCleanup cleanup;

    lifecycle.begin();
    assert(
        lifecycle.shutdown(cleanup) == CodexRuntimeCleanupAction::Shutdown);
    assert(
        lifecycle.disconnect(cleanup) == CodexRuntimeCleanupAction::None);
    assert((cleanup.events == std::vector<CleanupEvent>{
        CleanupEvent::DisconnectInputActions,
        CleanupEvent::ShutdownVoice,
        CleanupEvent::ShutdownVoiceProbe,
        CleanupEvent::StopTransport,
        CleanupEvent::ReleaseResources,
    }));
}

}  // namespace

int main()
{
    test_disconnect_and_shutdown_are_idempotent();
    test_new_connection_rearms_disconnect_cleanup();
    test_shutdown_covers_an_active_connection();
}
