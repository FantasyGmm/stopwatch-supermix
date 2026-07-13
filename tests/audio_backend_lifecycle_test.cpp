#include "hal/audio/audio_backend_lifecycle.h"
#include <cassert>

int main()
{
    AudioBackendLifecycle state;

    assert(state.openRecorder() == AudioOpenResult::Opened);
    assert(state.openRecorder() == AudioOpenResult::AlreadyOpen);
    assert(state.recorderOpen());
    assert(state.openPlayer() == AudioOpenResult::Opened);
    assert(state.openPlayer() == AudioOpenResult::AlreadyOpen);
    assert(state.playerOpen());

    assert(state.closeRecorder() == AudioCloseResult::Closed);
    assert(state.closeRecorder() == AudioCloseResult::AlreadyClosed);
    assert(!state.recorderOpen());
    assert(state.playerOpen());

    assert(state.closePlayer() == AudioCloseResult::Closed);
    assert(state.closePlayer() == AudioCloseResult::AlreadyClosed);
    assert(!state.playerOpen());

    return 0;
}
