#include "Focus.h"
#include "Ranking.h"

namespace rudder
{
std::optional<std::string> FocusTracker::pushed(const std::string& appId,
                                                const std::vector<RudderContext>& contexts,
                                                bool frontmost,
                                                std::int64_t nowMs)
{
    auto key = std::string {};

    for (auto& context: contexts)
        if (context.focused)
        {
            key = contextKey(appId, context);
            break;
        }

    auto& spell = spells[appId];

    if (spell.key == key)
        return std::nullopt;

    spell = {key, nowMs, false, false};

    if (!frontmost || key.empty())
        return std::nullopt;

    return key;
}

void FocusTracker::picked(const std::string& appId,
                          const std::string& key,
                          std::int64_t nowMs)
{
    spells[appId] = {key, nowMs, true, true};
}

std::optional<std::string> FocusTracker::activated(const std::string& appId,
                                                   std::int64_t nowMs)
{
    auto found = spells.find(appId);

    if (found == spells.end() || found->second.key.empty())
        return std::nullopt;

    auto& spell = found->second;

    // The pick's own activation -- Rudder records one as it opens the row,
    // and the OS reports it again a moment later.
    if (spell.picked && nowMs - spell.since < dwellMs)
        return spell.key;

    spell = {spell.key, nowMs, false, false};

    return spell.key;
}

std::optional<std::string> FocusTracker::settle(const std::string& frontAppId,
                                                std::int64_t nowMs)
{
    auto found = spells.find(frontAppId);

    if (found == spells.end())
        return std::nullopt;

    auto& spell = found->second;

    if (spell.key.empty() || spell.credited || nowMs - spell.since < dwellMs)
        return std::nullopt;

    spell.credited = true;
    return spell.key;
}

void FocusTracker::forget(const std::string& appId)
{
    spells.erase(appId);
}

void FocusTracker::forgetKey(const std::string& key)
{
    for (auto& [appId, spell]: spells)
        if (spell.key == key)
            spell = {};
}
} // namespace rudder
