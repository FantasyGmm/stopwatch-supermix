#include "main/apps/app_codex_status/codex_runtime_session.h"

#include <cassert>

namespace {

void test_disconnect_and_shutdown_are_idempotent()
{
    CodexRuntimeSessionLifecycle lifecycle;

    lifecycle.begin();
    assert(
        lifecycle.disconnect() ==
        CodexRuntimeCleanupAction::Disconnect);
    assert(
        lifecycle.disconnect() == CodexRuntimeCleanupAction::None);
    assert(
        lifecycle.shutdown() == CodexRuntimeCleanupAction::Shutdown);
    assert(lifecycle.shutdown() == CodexRuntimeCleanupAction::None);
}

void test_new_connection_rearms_disconnect_cleanup()
{
    CodexRuntimeSessionLifecycle lifecycle;

    lifecycle.begin();
    assert(
        lifecycle.disconnect() ==
        CodexRuntimeCleanupAction::Disconnect);
    lifecycle.begin();
    assert(
        lifecycle.disconnect() ==
        CodexRuntimeCleanupAction::Disconnect);
}

void test_shutdown_covers_an_active_connection()
{
    CodexRuntimeSessionLifecycle lifecycle;

    lifecycle.begin();
    assert(
        lifecycle.shutdown() == CodexRuntimeCleanupAction::Shutdown);
    assert(
        lifecycle.disconnect() == CodexRuntimeCleanupAction::None);
}

}  // namespace

int main()
{
    test_disconnect_and_shutdown_are_idempotent();
    test_new_connection_rearms_disconnect_cleanup();
    test_shutdown_covers_an_active_connection();
}
