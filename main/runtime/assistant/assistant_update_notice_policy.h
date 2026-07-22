#pragma once

#include <optional>
#include <string>
#include <string_view>

enum class AssistantUpdateStorageAction {
    None,
    Save,
    Clear,
};

std::optional<std::string> NormalizeAssistantFirmwareVersion(std::string_view value);

class AssistantUpdateNoticeModel {
public:
    explicit AssistantUpdateNoticeModel(std::string_view local_version);

    AssistantUpdateStorageAction restore(const std::optional<std::string>& stored_version);
    AssistantUpdateStorageAction observe(std::string_view remote_version);
    bool hasPending() const;
    const std::string& pendingVersion() const;
    std::string displayText() const;

private:
    std::optional<std::string> _local_version;
    std::string _pending_version;
};
