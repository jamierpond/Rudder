#pragma once

#include "Items.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
enum class PanelDumpReason
{
    summon,
    query,
    refresh
};

// Appends the panel's state and every provider push to <directory>/panel.jsonl,
// one JSON object per line, so a missing row can be traced after the fact.
class PanelDump
{
public:
    explicit PanelDump(std::optional<std::filesystem::path> directory);

    bool isEnabled() const;

    void writePush(std::int64_t atMs,
                   const std::string& appId,
                   const std::vector<RudderContext>& contexts);

    void writePanel(std::int64_t atMs,
                    PanelDumpReason reason,
                    const std::string& query,
                    const std::string& frontAppId,
                    int selected,
                    const std::vector<RudderItem>& rows,
                    const std::function<std::int64_t(const std::string&)>& getLastUsed,
                    const std::map<std::string, std::vector<RudderContext>>& providerContexts);

private:
    void appendLine(const std::string& line);

    std::optional<std::filesystem::path> file;
};
} // namespace rudder
