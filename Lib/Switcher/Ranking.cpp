#include "Ranking.h"
#include "FuzzyMatch.h"

#include <algorithm>
#include <unordered_set>
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

std::vector<RudderItem> mergeInstalled(std::vector<RudderItem> running,
                                       const std::vector<RudderItem>& installed)
{
    auto seen = std::unordered_set<std::string> {};

    for (auto& item: running)
        seen.insert(item.appId);

    for (auto& item: installed)
        if (seen.insert(item.appId).second)
        {
            running.push_back(item);
            running.back().running = false;
        }

    return running;
}

std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query)
{
    if (query.empty())
    {
        auto running = std::vector<RudderItem> {};

        for (auto& item: all)
            if (item.running)
                running.push_back(item);

        return running;
    }

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
                     [](const auto& a, const auto& b)
                     {
                         if (a.first != b.first)
                             return a.first > b.first;

                         return a.second.running && !b.second.running;
                     });

    auto ranked = std::vector<RudderItem> {};

    for (auto& [score, item]: scored)
        ranked.push_back(std::move(item));

    return ranked;
}

PreviewAction previewActionFor(const RudderItem& item,
                               bool runningNow,
                               const std::string& originAppId)
{
    if (!item.running || !runningNow)
        return PreviewAction::none;

    return item.appId == originAppId ? PreviewAction::restoreOrigin
                                     : PreviewAction::bringForward;
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
