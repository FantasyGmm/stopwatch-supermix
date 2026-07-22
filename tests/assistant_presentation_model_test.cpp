#include "apps/app_assistant/assistant_presentation_model.h"

#include <cassert>

int main()
{
    AssistantPresentationModel model;
    assert(model.state() == AssistantVisualState::Connecting);

    model.apply(AssistantPresentationSignal::Activating);
    assert(model.state() == AssistantVisualState::Activating);

    model.apply(AssistantPresentationSignal::Running);
    assert(model.state() == AssistantVisualState::WaitingWake);

    model.apply(AssistantPresentationSignal::ListeningChanged, false);
    assert(model.state() == AssistantVisualState::WaitingWake);
    model.apply(AssistantPresentationSignal::ListeningChanged, true);
    assert(model.state() == AssistantVisualState::Listening);

    model.apply(AssistantPresentationSignal::UserText);
    assert(model.state() == AssistantVisualState::Thinking);
    model.apply(AssistantPresentationSignal::ListeningChanged, false);
    assert(model.state() == AssistantVisualState::Thinking);

    model.apply(AssistantPresentationSignal::AgentText);
    assert(model.state() == AssistantVisualState::Speaking);
    model.apply(AssistantPresentationSignal::SpeakingChanged, false);
    assert(model.state() == AssistantVisualState::WaitingWake);

    model.apply(AssistantPresentationSignal::Closing);
    assert(model.state() == AssistantVisualState::Closing);
    model.apply(AssistantPresentationSignal::ListeningChanged, true);
    assert(model.state() == AssistantVisualState::Closing);
    model.apply(AssistantPresentationSignal::AgentText);
    assert(model.state() == AssistantVisualState::Closing);

    model.reset();
    model.apply(AssistantPresentationSignal::Failed);
    assert(model.state() == AssistantVisualState::Failed);
    model.apply(AssistantPresentationSignal::SpeakingChanged, false);
    assert(model.state() == AssistantVisualState::Failed);
    model.apply(AssistantPresentationSignal::Connecting);
    assert(model.state() == AssistantVisualState::Connecting);
    model.apply(AssistantPresentationSignal::Failed);
    model.apply(AssistantPresentationSignal::StopTimedOut);
    assert(model.state() == AssistantVisualState::StopTimedOut);
    return 0;
}
