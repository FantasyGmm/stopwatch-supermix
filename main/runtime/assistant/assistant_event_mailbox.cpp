/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_event_mailbox.h"

#include <algorithm>
#include <utility>

AssistantEventMailbox::AssistantEventMailbox(std::size_t transient_capacity)
    : _capacity(transient_capacity)
{
}

void AssistantEventMailbox::publish(AssistantEvent event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    Envelope envelope{++_sequence, std::move(event)};
    const int persistent_index = persistentIndex(envelope.event.kind);
    if (persistent_index >= 0) {
        _persistent[static_cast<std::size_t>(persistent_index)] = std::move(envelope);
        return;
    }

    if (_capacity == 0) {
        ++_dropped;
        return;
    }
    if (_transient.size() == _capacity) {
        _transient.pop_front();
        ++_dropped;
    }
    _transient.push_back(std::move(envelope));
}

bool AssistantEventMailbox::poll(AssistantEvent& event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    Envelope* next = _transient.empty() ? nullptr : &_transient.front();
    std::optional<Envelope>* persistent_next = nullptr;

    for (auto& slot : _persistent) {
        if (!slot.has_value()) {
            continue;
        }
        if (next == nullptr || slot->sequence < next->sequence) {
            next = &slot.value();
            persistent_next = &slot;
        }
    }

    if (next == nullptr) {
        return false;
    }

    event = std::move(next->event);
    if (persistent_next != nullptr) {
        persistent_next->reset();
    } else {
        _transient.pop_front();
    }
    return true;
}

void AssistantEventMailbox::discardSessionsBefore(uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& slot : _persistent) {
        if (slot.has_value() && slot->event.session_id < session_id) {
            slot.reset();
        }
    }
    _transient.erase(
        std::remove_if(_transient.begin(), _transient.end(), [session_id](const Envelope& envelope) {
            return envelope.event.session_id < session_id;
        }),
        _transient.end());
}

std::size_t AssistantEventMailbox::droppedTransientCount() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _dropped;
}

int AssistantEventMailbox::persistentIndex(AssistantEventKind kind)
{
    switch (kind) {
        case AssistantEventKind::State:
            return 0;
        case AssistantEventKind::ActivationCode:
            return 1;
        case AssistantEventKind::Error:
            return 2;
        case AssistantEventKind::UpdateAvailable:
            return 3;
        case AssistantEventKind::UserText:
        case AssistantEventKind::AgentText:
        case AssistantEventKind::Emotion:
            return -1;
    }
    return -1;
}
