/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

enum class AssistantEventKind : uint8_t {
    State,
    ActivationCode,
    Error,
    UserText,
    AgentText,
    Emotion,
};

struct AssistantEvent {
    uint64_t session_id = 0;
    AssistantEventKind kind = AssistantEventKind::State;
    std::string text;
};

class AssistantEventMailbox {
public:
    explicit AssistantEventMailbox(std::size_t transient_capacity = 32);

    void publish(AssistantEvent event);
    bool poll(AssistantEvent& event);
    void discardSessionsBefore(uint64_t session_id);
    std::size_t droppedTransientCount() const;

private:
    struct Envelope {
        uint64_t sequence;
        AssistantEvent event;
    };

    static int persistentIndex(AssistantEventKind kind);

    mutable std::mutex _mutex;
    std::array<std::optional<Envelope>, 3> _persistent;
    std::deque<Envelope> _transient;
    std::size_t _capacity;
    std::size_t _dropped = 0;
    uint64_t _sequence = 0;
};
