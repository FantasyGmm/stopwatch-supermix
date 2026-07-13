#include "runtime/assistant/assistant_event_mailbox.h"

#include <cassert>
#include <vector>

namespace {
std::vector<AssistantEvent> drain(AssistantEventMailbox& mailbox)
{
    std::vector<AssistantEvent> events;
    AssistantEvent event;
    while (mailbox.poll(event)) {
        events.push_back(event);
    }
    return events;
}
}  // namespace

int main()
{
    AssistantEventMailbox mailbox(2);
    mailbox.publish({1, AssistantEventKind::State, "Starting"});
    mailbox.publish({1, AssistantEventKind::UserText, "one"});
    mailbox.publish({1, AssistantEventKind::ActivationCode, "123456"});
    mailbox.publish({1, AssistantEventKind::State, "Running"});
    mailbox.publish({1, AssistantEventKind::AgentText, "two"});
    mailbox.publish({1, AssistantEventKind::Emotion, "three"});
    mailbox.publish({1, AssistantEventKind::Error, "recoverable"});

    assert(mailbox.droppedTransientCount() == 1);
    auto events = drain(mailbox);
    assert(events.size() == 5);
    assert(events[0].kind == AssistantEventKind::ActivationCode);
    assert(events[0].text == "123456");
    assert(events[1].kind == AssistantEventKind::State);
    assert(events[1].text == "Running");
    assert(events[2].kind == AssistantEventKind::AgentText);
    assert(events[2].text == "two");
    assert(events[3].kind == AssistantEventKind::Emotion);
    assert(events[3].text == "three");
    assert(events[4].kind == AssistantEventKind::Error);
    assert(events[4].text == "recoverable");

    mailbox.publish({1, AssistantEventKind::Error, "old"});
    mailbox.publish({1, AssistantEventKind::ActivationCode, "obsolete code"});
    mailbox.publish({2, AssistantEventKind::UserText, "current text"});
    mailbox.publish({2, AssistantEventKind::Error, "current error"});
    mailbox.discardSessionsBefore(2);
    events = drain(mailbox);
    assert(events.size() == 2);
    assert(events[0].session_id == 2);
    assert(events[0].kind == AssistantEventKind::UserText);
    assert(events[1].session_id == 2);
    assert(events[1].kind == AssistantEventKind::Error);

    AssistantEventMailbox persistent_only(0);
    persistent_only.publish({7, AssistantEventKind::UserText, "dropped"});
    persistent_only.publish({7, AssistantEventKind::State, "Running"});
    assert(persistent_only.droppedTransientCount() == 1);
    events = drain(persistent_only);
    assert(events.size() == 1);
    assert(events[0].kind == AssistantEventKind::State);
    assert(events[0].text == "Running");
    return 0;
}
