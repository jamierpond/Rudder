#include "Ranking.h"
#include "FuzzyMatch.h"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

namespace rudder
{
namespace
{
RudderItem contextRow(const RudderItem& app, const RudderContext& context)
{
    auto row = RudderItem {};
    row.kind = context.kind.empty() ? "context" : context.kind;
    row.id = app.id + "/" + context.id;
    row.title = context.title;
    row.subtitle = context.subtitle.empty() ? app.title
                                            : app.title + " · " + context.subtitle;
    row.appId = app.id;
    row.contextId = context.id;
    row.url = context.url;

    // A place that isn't open yet reads like a closed app and, like one, is
    // never previewed: activating it may open something.
    row.running = context.open;
    return row;
}

// What a favourite is found by: its title, its keywords and its target.
std::string favouriteText(const RudderItem& item)
{
    auto text = item.title;

    for (auto& keyword: item.keywords)
        text += " " + keyword;

    return text + " " + item.subtitle;
}

// A context matched by its app's name alone ("wim" against every Wim tab)
// sits below the app row and below contexts whose own text matched.
constexpr auto appNameOnlyPenalty = 8;

// How well `query` finds a context: some leading part of the query on the
// app's name and the rest on the context's own title and url, best split
// wins. Scoring the halves separately keeps app-name letters from stealing
// the query: matched greedily against "CowTerm ~/projects/Wim", "wim" took
// the w of CowTerm and scored like any pane with a w, i and m in it, and a
// project typed after the app name ("cowtermwim") ranked by where those
// letters first turned up rather than by the directory that spells them.
std::optional<int> contextScore(const std::string& query,
                                const RudderItem& app,
                                const RudderContext& context)
{
    const auto own = context.title + " " + context.url;
    auto best = wim::fuzzyScore(query, own);

    for (std::size_t split = 1; split <= query.size(); ++split)
    {
        auto onApp = wim::fuzzyScore(query.substr(0, split), app.title);

        if (!onApp)
            break;

        const auto rest = query.substr(split);

        if (rest.find_first_not_of(' ') == std::string::npos)
        {
            auto score = *onApp - appNameOnlyPenalty;

            if (!best || score > *best)
                best = score;

            continue;
        }

        if (auto onContext = wim::fuzzyScore(rest, own))
            if (!best || *onApp + *onContext > *best)
                best = *onApp + *onContext;
    }

    return best;
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
        auto browsing = std::vector<RudderItem> {};

        for (auto& item: all)
            if (item.kind == "favourite" || item.running)
                browsing.push_back(item);

        return browsing;
    }

    auto ranked = std::vector<RudderItem> {};

    for (auto& [score, item]: scoreItems(all, query))
        ranked.push_back(std::move(item));

    return ranked;
}

std::vector<ScoredItem> scoreItems(const std::vector<RudderItem>& all,
                                   const std::string& query)
{
    auto scored = std::vector<ScoredItem> {};

    for (auto& app: all)
    {
        if (app.kind == "favourite")
        {
            // Favourites are what the user asked to find first.
            if (auto score = wim::fuzzyScore(query, favouriteText(app)))
                scored.push_back({*score + favouriteBoost, app});

            continue;
        }

        if (auto score = wim::fuzzyScore(query, app.title))
            scored.push_back({*score, app});

        // A context matches on its app's name, its title and its url, so
        // "wim github" finds the GitHub tab in Wim.
        for (auto& context: app.contexts)
            if (auto score = contextScore(query, app, context))
                scored.push_back({*score, contextRow(app, context)});
    }

    std::stable_sort(scored.begin(),
                     scored.end(),
                     [](const auto& a, const auto& b)
                     {
                         if (a.score != b.score)
                             return a.score > b.score;

                         return a.item.running && !b.item.running;
                     });

    return scored;
}

PreviewAction previewActionFor(const RudderItem& item,
                               bool runningNow,
                               const std::string& originAppId)
{
    // A web row opens a page, so only Enter does it.
    if (item.kind == "web" || !item.running || !runningNow)
        return PreviewAction::none;

    return item.appId == originAppId ? PreviewAction::restoreOrigin
                                     : PreviewAction::bringForward;
}

int initialSelection(const std::vector<RudderItem>& items,
                     const std::string& query,
                     const std::string& frontAppId)
{
    if (!query.empty())
        return 0;

    // Favourites sit above the apps; cmd-tab behaviour is about the apps.
    auto first = (int) std::count_if(items.begin(),
                                     items.end(),
                                     [](const RudderItem& item)
                                     { return item.kind == "favourite"; });

    if (first >= (int) items.size())
        return 0;

    auto skipFront = items[(std::size_t) first].id == frontAppId
                     && first + 1 < (int) items.size();
    return first + (skipFront ? 1 : 0);
}
} // namespace rudder
