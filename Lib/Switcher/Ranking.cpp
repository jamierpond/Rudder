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

// A context matched by its app's name alone ("grazer" against every Grazer
// tab) sits below the app row and below contexts whose own text matched.
constexpr auto appNameOnlyPenalty = 8;

// Taken off a match on a context's other fields, so on an equal match the
// title wins, then the directory's name, then the subtitle, then the full
// path: title >= dirname > subtitle > path.
constexpr auto dirNamePenalty = 1;
constexpr auto subtitlePenalty = 6;
constexpr auto pathPenalty = 12;

// The subtitle and the full path are long and alike from row to row ("Tab 3
// · pane 1 · main", "/Users/me/projects/..."), so letters scattered through
// them match almost anything. They count only for a tight match: at least
// this much score per letter typed, about half of what a run of consecutive
// letters earns.
constexpr auto tightPerLetter = 4;

int letterCount(const std::string& query)
{
    return (int) std::count_if(query.begin(), query.end(), [](char c) { return c != ' '; });
}

std::string lastComponent(std::string path)
{
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// How well `query` finds a context by its own text: the title and url as
// they read, the name of its directory nearly as well (so a pane is found by
// its project whatever the program in it set its title to), and the subtitle
// and full path below both, and only on a tight match.
std::optional<int> ownScore(const std::string& query, const RudderContext& context)
{
    auto best = grazer::fuzzyScore(query, context.title + " " + context.url);

    auto consider = [&](const std::string& text, int penalty, bool tightOnly)
    {
        if (text.empty())
            return;

        auto score = grazer::fuzzyScore(query, text);

        if (!score || (tightOnly && *score < tightPerLetter * letterCount(query)))
            return;

        if (!best || *score - penalty > *best)
            best = *score - penalty;
    };

    consider(lastComponent(context.path), dirNamePenalty, false);
    consider(context.subtitle, subtitlePenalty, true);
    consider(context.path, pathPenalty, true);
    return best;
}

// How well `query` finds a context: some leading part of the query on the
// app's name and the rest on the context's own text (ownScore), best split
// wins. Scoring the halves separately keeps app-name letters from stealing
// the query: matched greedily against "CowTerm ~/projects/Wim" (Grazer's old
// name), "wim" took the w of CowTerm and scored like any pane with a w, i and m
// in it, and a project typed after the app name ("cowtermwim") ranked by where those
// letters first turned up rather than by the directory that spells them.
std::optional<int> contextScore(const std::string& query,
                                const RudderItem& app,
                                const RudderContext& context)
{
    auto best = ownScore(query, context);

    for (std::size_t split = 1; split <= query.size(); ++split)
    {
        auto onApp = grazer::fuzzyScore(query.substr(0, split), app.title);

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

        if (auto onContext = ownScore(rest, context))
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
            if (auto score = grazer::fuzzyScore(query, favouriteText(app)))
                scored.push_back({*score + favouriteBoost, app});

            continue;
        }

        if (auto score = grazer::fuzzyScore(query, app.title))
            scored.push_back({*score, app});

        // A context matches on its app's name, its title and url, its
        // directory and its subtitle, so "grazer github" finds the GitHub tab
        // in Grazer and "grazer" the pane sitting in ~/projects/Grazer.
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

    // Only the app row itself shows the screen as it was. Its windows and
    // its contexts (a CowTerm pane while summoned over CowTerm) move it like
    // any other app's: to that window, or to that pane or tab.
    return item.kind == "app" && item.appId == originAppId ? PreviewAction::restoreOrigin
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
