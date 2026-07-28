#pragma once

#include <atomic>
#include <cstdint>

class TouchInputGate {
public:
    void armForScreenOff()
    {
        State state = _state.load(std::memory_order_acquire);
        while (true) {
            switch (state) {
                case State::Open:
                case State::ReleaseAfterSample:
                    if (_state.compare_exchange_weak(
                            state, State::Armed,
                            std::memory_order_acq_rel)) {
                        return;
                    }
                    break;
                case State::Suppressing:
                    if (_state.compare_exchange_weak(
                            state, State::SuppressingArmed,
                            std::memory_order_acq_rel)) {
                        return;
                    }
                    break;
                case State::Armed:
                case State::SuppressingArmed:
                    return;
            }
        }
    }

    void releaseAfterNextSample()
    {
        State state = _state.load(std::memory_order_acquire);
        while (true) {
            switch (state) {
                case State::Armed:
                    if (_state.compare_exchange_weak(
                            state, State::ReleaseAfterSample,
                            std::memory_order_acq_rel)) {
                        return;
                    }
                    break;
                case State::SuppressingArmed:
                    if (_state.compare_exchange_weak(
                            state, State::Suppressing,
                            std::memory_order_acq_rel)) {
                        return;
                    }
                    break;
                case State::Open:
                case State::ReleaseAfterSample:
                case State::Suppressing:
                    return;
            }
        }
    }

    void prepareForActiveDisplay()
    {
        releaseAfterNextSample();
    }

    bool filterForLvgl(bool pressed)
    {
        State state = _state.load(std::memory_order_acquire);
        while (true) {
            switch (state) {
                case State::Open:
                    _suppressed_this_frame.store(false,
                                                  std::memory_order_release);
                    return pressed;
                case State::Armed:
                    if (!pressed) {
                        _suppressed_this_frame.store(
                            false, std::memory_order_release);
                        return false;
                    }
                    if (_state.compare_exchange_weak(
                            state, State::Suppressing,
                            std::memory_order_acq_rel)) {
                        requestWake();
                        return false;
                    }
                    break;
                case State::ReleaseAfterSample:
                    if (!pressed) {
                        if (_state.compare_exchange_weak(
                                state, State::Open,
                                std::memory_order_acq_rel)) {
                            _suppressed_this_frame.store(
                                false, std::memory_order_release);
                            return false;
                        }
                        break;
                    }
                    if (_state.compare_exchange_weak(
                            state, State::Suppressing,
                            std::memory_order_acq_rel)) {
                        requestWake();
                        return false;
                    }
                    break;
                case State::Suppressing:
                case State::SuppressingArmed:
                    _suppressed_this_frame.store(
                        true, std::memory_order_release);
                    if (!pressed) {
                        const State next =
                            state == State::SuppressingArmed
                            ? State::Armed
                            : State::Open;
                        if (!_state.compare_exchange_weak(
                                state, next,
                                std::memory_order_acq_rel)) {
                            break;
                        }
                    }
                    return false;
            }
        }
    }

    bool consumeWakeRequest()
    {
        return _wake_requested.exchange(false, std::memory_order_acq_rel);
    }

    bool suppressedThisFrame() const
    {
        return _suppressed_this_frame.load(std::memory_order_acquire);
    }

    bool suppressing() const
    {
        const State state = _state.load(std::memory_order_acquire);
        return state == State::Suppressing ||
               state == State::SuppressingArmed;
    }

private:
    enum class State : uint8_t {
        Open,
        Armed,
        ReleaseAfterSample,
        Suppressing,
        SuppressingArmed,
    };

    void requestWake()
    {
        _wake_requested.store(true, std::memory_order_release);
        _suppressed_this_frame.store(true, std::memory_order_release);
    }

    std::atomic<State> _state{State::Open};
    std::atomic<bool> _wake_requested{false};
    std::atomic<bool> _suppressed_this_frame{false};
};
