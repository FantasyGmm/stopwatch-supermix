#pragma once

#include <cstdint>

enum class ExternalPowerState : uint8_t { Unknown, Battery, ExternalPower };

struct ExternalPowerSnapshot {
    ExternalPowerState state = ExternalPowerState::Unknown;
    uint32_t revision        = 0;
    bool latest_sample_valid = false;
};

struct ExternalPowerUpdate {
    ExternalPowerSnapshot snapshot;
    bool changed = false;
};

class ExternalPowerTracker {
public:
    ExternalPowerUpdate observe(bool valid, bool inserted)
    {
        if (!valid) {
            _snapshot.latest_sample_valid = false;
            _battery_confirmations        = 0;
            return {_snapshot, false};
        }

        _snapshot.latest_sample_valid = true;
        if (inserted) {
            _battery_confirmations = 0;
            return transitionTo(ExternalPowerState::ExternalPower);
        }

        if (_battery_confirmations < 3) {
            ++_battery_confirmations;
        }
        if (_battery_confirmations == 3) {
            return transitionTo(ExternalPowerState::Battery);
        }

        return {_snapshot, false};
    }

    ExternalPowerSnapshot snapshot() const
    {
        return _snapshot;
    }

private:
    ExternalPowerUpdate transitionTo(ExternalPowerState state)
    {
        if (_snapshot.state == state) {
            return {_snapshot, false};
        }

        _snapshot.state = state;
        ++_snapshot.revision;
        return {_snapshot, true};
    }

    ExternalPowerSnapshot _snapshot;
    uint8_t _battery_confirmations = 0;
};
