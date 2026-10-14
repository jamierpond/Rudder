#include "Ranking.h"
#include "FuzzyMatch.h"

#include <algorithm>
#include <utility>

namespace rudder
{
namespace
{
RudderItem contextRow(const RudderItem& app, const RudderContext& context)
{
    auto row = RudderItem {};
    row.kind = "context";
    row.id = app.id + "/" + context.id;
    row.title = context.title;
    row.subtitle = context.subtitle.empty() ? app.title
                                            : app.title + " · " + context.subtitle;
    row.appId = app.id;
    row.contextId = context.id;
    return row;
}
} // namespace

std::vector<RudderItem> orderByRecency(std::vector<RudderItem> apps,
                                       const LastUsedFn& lastUsed)
{
    std::stable_sort(apps.begin(),
                     apps.end(),
                     [&](const RudderItem& a, const RudderItem& b)
                     { return lastUsed(a.id) > lastUsed(b.id); });

    return apps;
}

std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query)
{
    if (query.empty())
        return all;

    auto scored = std::vector<std::pair<int, RudderItem>> {};

    for (auto& app: all)
    {
        if (auto score = wim::fuzzyScore(query, app.title))
            scored.push_back({*score, app});

        // A context matches on its own title or its app's name, so "wim
        // github" finds the GitHub tab in Wim.
        for (auto& context: app.contexts)
            if (auto score = wim::fuzzyScore(query, context.title + " " + app.title))
                scored.push_back({*score, contextRow(app, context)});
    }

    std::stable_sort(scored.begin(),
                     scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    auto ranked = std::vector<RudderItem> {};

    for (auto& [score, item]: scored)
        ranked.push_back(std::move(item));

    return ranked;
}

int initialSelection(const std::vector<RudderItem>& items,
                     const std::string& query,
                     const std::string& frontAppId)
{
    if (!query.empty() || items.size() < 2)
        return 0;

    return items.front().id == frontAppId ? 1 : 0;
}
} // namespace rudder
