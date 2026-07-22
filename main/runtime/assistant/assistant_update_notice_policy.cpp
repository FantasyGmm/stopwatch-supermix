#include "runtime/assistant/assistant_update_notice_policy.h"

#include <array>
#include <cstdint>
#include <limits>

namespace {

struct FirmwareVersion {
    std::array<std::uint64_t, 3> components{};
};

std::optional<FirmwareVersion> ParseFirmwareVersion(std::string_view value)
{
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
        value.remove_prefix(1);
    }

    if (value.empty()) {
        return std::nullopt;
    }

    FirmwareVersion version;
    std::size_t component_index = 0;
    std::size_t component_start = 0;
    for (std::size_t index = 0; index <= value.size(); ++index) {
        if (index != value.size() && value[index] != '.') {
            continue;
        }

        if (component_index == version.components.size() || index == component_start) {
            return std::nullopt;
        }

        std::uint64_t component = 0;
        for (std::size_t digit_index = component_start; digit_index < index; ++digit_index) {
            const char digit = value[digit_index];
            if (digit < '0' || digit > '9') {
                return std::nullopt;
            }

            const std::uint64_t digit_value = static_cast<std::uint64_t>(digit - '0');
            if (component > (std::numeric_limits<std::uint64_t>::max() - digit_value) / 10) {
                return std::nullopt;
            }
            component = component * 10 + digit_value;
        }

        version.components[component_index++] = component;
        component_start = index + 1;
    }

    return version;
}

bool IsNewer(const FirmwareVersion& candidate, const FirmwareVersion& baseline)
{
    for (std::size_t index = 0; index < candidate.components.size(); ++index) {
        if (candidate.components[index] != baseline.components[index]) {
            return candidate.components[index] > baseline.components[index];
        }
    }
    return false;
}

std::string FormatFirmwareVersion(const FirmwareVersion& version)
{
    return std::to_string(version.components[0]) + "." +
           std::to_string(version.components[1]) + "." +
           std::to_string(version.components[2]);
}

std::optional<FirmwareVersion> ParseCanonicalFirmwareVersion(const std::string& value)
{
    return ParseFirmwareVersion(value);
}

} // namespace

std::optional<std::string> NormalizeAssistantFirmwareVersion(std::string_view value)
{
    const auto version = ParseFirmwareVersion(value);
    if (!version) {
        return std::nullopt;
    }
    return FormatFirmwareVersion(*version);
}

AssistantUpdateNoticeModel::AssistantUpdateNoticeModel(std::string_view local_version)
    : _local_version(NormalizeAssistantFirmwareVersion(local_version))
{
}

AssistantUpdateStorageAction AssistantUpdateNoticeModel::restore(
    const std::optional<std::string>& stored_version)
{
    if (!stored_version) {
        return AssistantUpdateStorageAction::None;
    }

    const auto stored = ParseFirmwareVersion(*stored_version);
    const auto local = _local_version ? ParseCanonicalFirmwareVersion(*_local_version) : std::nullopt;
    if (!stored) {
        return AssistantUpdateStorageAction::Clear;
    }

    _pending_version = FormatFirmwareVersion(*stored);
    if (!local) {
        return AssistantUpdateStorageAction::None;
    }
    if (!IsNewer(*stored, *local)) {
        _pending_version.clear();
        return AssistantUpdateStorageAction::Clear;
    }
    return AssistantUpdateStorageAction::None;
}

AssistantUpdateStorageAction AssistantUpdateNoticeModel::observe(std::string_view remote_version)
{
    const auto remote = ParseFirmwareVersion(remote_version);
    const auto local = _local_version ? ParseCanonicalFirmwareVersion(*_local_version) : std::nullopt;
    if (!remote || !local || !IsNewer(*remote, *local)) {
        return AssistantUpdateStorageAction::None;
    }

    if (!_pending_version.empty()) {
        const auto pending = ParseCanonicalFirmwareVersion(_pending_version);
        if (pending && !IsNewer(*remote, *pending)) {
            return AssistantUpdateStorageAction::None;
        }
    }

    _pending_version = FormatFirmwareVersion(*remote);
    return AssistantUpdateStorageAction::Save;
}

bool AssistantUpdateNoticeModel::hasPending() const
{
    return !_pending_version.empty();
}

const std::string& AssistantUpdateNoticeModel::pendingVersion() const
{
    return _pending_version;
}

std::string AssistantUpdateNoticeModel::displayText() const
{
    if (_pending_version.empty()) {
        return "";
    }
    return "发现新版本 v" + _pending_version;
}
