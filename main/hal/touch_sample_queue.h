#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct TouchSample {
    bool pressed = false;
    int x = -1;
    int y = -1;
    uint32_t timestamp_ms = 0;
};

constexpr uint32_t touch_sampling_period_ms(bool contact_active)
{
    return contact_active ? 10U : 33U;
}

class TouchSampleQueue {
public:
    static constexpr std::size_t kCapacity = 32;

    static constexpr std::size_t capacity()
    {
        return kCapacity;
    }

    bool push(const TouchSample& sample)
    {
        if (_size != 0) {
            TouchSample& tail = at(_size - 1);
            if (tail.pressed == sample.pressed) {
                if (!sample.pressed ||
                    (tail.x == sample.x && tail.y == sample.y)) {
                    return false;
                }
            }
        }

        if (_size < capacity()) {
            at(_size) = sample;
            ++_size;
            return true;
        }

        TouchSample& tail = at(_size - 1);
        if (!sample.pressed || tail.pressed == sample.pressed) {
            tail = sample;
            ++_coalesced;
            return true;
        }

        ++_dropped;
        return false;
    }

    bool pop_for_lvgl(TouchSample& sample)
    {
        if (_size == 0) {
            return false;
        }

        if (!_delivered_pressed) {
            sample = pop_front();
            _delivered_pressed = sample.pressed;
            return true;
        }

        if (!at(0).pressed) {
            sample = pop_front();
            _delivered_pressed = false;
            return true;
        }

        std::size_t newest_pressed = 0;
        while (newest_pressed + 1 < _size &&
               at(newest_pressed + 1).pressed) {
            ++newest_pressed;
        }
        sample = at(newest_pressed);
        remove_front(newest_pressed + 1);
        return true;
    }

    bool has_pending() const
    {
        return _size != 0;
    }

    void clear()
    {
        _head = 0;
        _size = 0;
        _delivered_pressed = false;
    }

    std::size_t dropped_samples() const
    {
        return _dropped;
    }

    std::size_t coalesced_samples() const
    {
        return _coalesced;
    }

private:
    TouchSample& at(std::size_t logical_index)
    {
        return _samples[(_head + logical_index) % capacity()];
    }

    const TouchSample& at(std::size_t logical_index) const
    {
        return _samples[(_head + logical_index) % capacity()];
    }

    TouchSample pop_front()
    {
        const TouchSample sample = at(0);
        remove_front(1);
        return sample;
    }

    void remove_front(std::size_t count)
    {
        _head = (_head + count) % capacity();
        _size -= count;
    }

    std::array<TouchSample, kCapacity> _samples{};
    std::size_t _head = 0;
    std::size_t _size = 0;
    std::size_t _dropped = 0;
    std::size_t _coalesced = 0;
    bool _delivered_pressed = false;
};
