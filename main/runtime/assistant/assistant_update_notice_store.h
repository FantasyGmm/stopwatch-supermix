#pragma once

#include <optional>
#include <string>
#include <string_view>

class AssistantUpdateNoticeStore {
public:
    std::optional<std::string> load() const;
    bool save(std::string_view version) const;
    bool clear() const;
};
