#include "Ranking.h"
#include "FuzzyMatch.h"

#include <algorithm>
#include <cmath>
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

    // Renamed: the user's name is the title, the provider's leads the
    // subtitle.
    if (!context.name.empty())
    {
        row.title = context.name;
        row.subtitle = context.title + " · " + row.subtitle;
    }

    row.appId = app.id;
    row.contextId = context.id;
    row.url = context.url;
    row.branch = context.branch;
    row.contextPath = context.path;

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

// A context's git branch is its most telling name: a match on it gets this
// on top, so it beats an equal match on the title.
constexpr auto branchBonus = 1;

// Taken off a match on a context's other fields, so on an equal match the
// title wins, then the directory's name and the subtitle, then the full
// path: branch > title > dirname = subtitle > path. The directory's name
// (a worktree's, a project's) says little the title and branch don't.
constexpr auto dirNamePenalty = 6;
constexpr auto subtitlePenalty = 6;
constexpr auto pathPenalty = 12;

// The directory's name, the subtitle and the full path are alike from row to
// row ("Tab 3 · pane 1 · main", "/Users/me/projects/..."), so letters
// scattered through them match almost anything. They count only for a tight match: at least
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

// How well `query` finds a context by its own text: its git branch and the
// name the user gave it best, then the (provider's) title and url as they read, then the name of its directory and
// the subtitle, then the full path, those three only on a tight match.
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

    consider(context.branch, -branchBonus, false);
    consider(context.name, -branchBonus, false);
    consider(lastComponent(context.path), dirNamePenalty, true);
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

namespace
{
std::string stableKey(const std::string& appId,
                      const std::string& branch,
                      const std::string& path,
                      const std::string& id)
{
    if (!branch.empty())
        return appId + "/branch:" + branch;

    if (!path.empty())
        return appId + "/path:" + path;

    return appId + "/" + id;
}
} // namespace

std::string contextKey(const std::string& appId, const RudderContext& context)
{
    return stableKey(appId, context.branch, context.path, context.id);
}

std::string frecencyKey(const RudderItem& item)
{
    if (item.kind == "context" || item.kind == "window")
        return stableKey(item.appId, item.branch, item.contextPath, item.contextId);

    if (item.kind != "favourite")
        return item.id;

    auto target = !item.url.empty()    ? item.url
                  : !item.path.empty() ? item.path
                  : !item.appId.empty() ? item.appId
                                        : item.title;
    return "fav:" + target;
}

double frecencyBonus(double frecency)
{
    if (frecency <= 0)
        return 0;

    return std::min(frecencyCap, frecencyK * std::log2(1 + frecency));
}

std::vector<RudderItem> orderByRecency(std::vector<RudderItem> apps,
                                       const LastUsedFn& lastUsed)
{
    std::stable_sort(apps.begin(),
                     apps.end(),
                     [&](const RudderItem& a, const RudderItem& b)
                     { return lastUsed(frecencyKey(a)) > lastUsed(frecencyKey(b)); });

    return apps;
}

std::vector<RudderItem> mergeInstalled(std::vector<RudderItem> running,
                                       const std::vector<RudderItem>& installed)
{
    auto seen = std::unordered_set<std::string> {};

    for (auto& item: running)
        if (item.kind != "favourite")
            seen.insert(item.appId);

    for (auto& item: installed)
        if (seen.insert(item.appId).second)
        {
            running.push_back(item);
            running.back().running = false;
        }

    return running;
}

std::vector<RudderItem> browseItems(const std::vector<RudderItem>& all,
                                    const Recency& recency)
{
    struct Recent
    {
        std::int64_t at = 0;
        std::string key;
        RudderItem row;
    };

    // The contexts used lately, one per key: the open one where a pane and
    // its branch's place both turn up.
    auto recent = std::vector<Recent> {};

    for (auto& app: all)
    {
        if (app.kind != "app" || !app.running)
            continue;

        for (auto& context: app.contexts)
        {
            if (context.kind == "window"
                || (app.id == recency.frontAppId && context.focused))
                continue;

            auto key = contextKey(app.id, context);
            auto at = recency.lastUsed(key);

            if (at <= 0 || recency.nowMs - at > recentContextWindowMs)
                continue;

            auto same = std::find_if(recent.begin(),
                                     recent.end(),
                                     [&](const Recent& other) { return other.key == key; });

            if (same == recent.end())
                recent.push_back({at, key, contextRow(app, context)});
            else if (!same->row.running && context.open)
                same->row = contextRow(app, context);
        }
    }

    // The front app's focused pane is left out above; its other rows on the
    // same key (another pane on that branch) are where the user is too.
    if (auto front = std::find_if(all.begin(),
                                  all.end(),
                                  [&](const RudderItem& item)
                                  { return item.kind == "app" && item.id == recency.frontAppId; });
        front != all.end())
        for (auto& context: front->contexts)
            if (context.focused)
                std::erase_if(recent,
                              [&](const Recent& other)
                              { return other.key == contextKey(front->id, context); });

    std::stable_sort(recent.begin(),
                     recent.end(),
                     [](const Recent& a, const Recent& b) { return a.at > b.at; });

    if (recent.size() > recentContextLimit)
        recent.resize(recentContextLimit);

    // Each app's recent contexts just before it, so on an equal stamp (the
    // activation that stamped both) the context comes first; then everything
    // by recency.
    auto rows = std::vector<std::pair<std::int64_t, RudderItem>> {};

    for (auto& item: all)
    {
        if (item.kind != "favourite" && !item.running)
            continue;

        for (auto& context: recent)
            if (item.kind == "app" && context.row.appId == item.id)
                rows.push_back({context.at, context.row});

        rows.push_back({recency.lastUsed(frecencyKey(item)), item});
    }

    std::stable_sort(rows.begin(),
                     rows.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    auto browsing = std::vector<RudderItem> {};

    for (auto& [at, row]: rows)
        browsing.push_back(std::move(row));

    return browsing;
}

std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query,
                                  const FrecencyFn& frecency,
                                  const Recency& recency)
{
    if (query.empty() && recency.lastUsed)
        return browseItems(all, recency);

    if (query.empty())
    {
        auto browsing = std::vector<RudderItem> {};

        for (auto& item: all)
            if (item.kind == "favourite" || item.running)
                browsing.push_back(item);

        return browsing;
    }

    auto ranked = std::vector<RudderItem> {};

    for (auto& scored: scoreItems(all, query, frecency))
        ranked.push_back(std::move(scored.item));

    return ranked;
}

std::vector<ScoredItem> scoreItems(const std::vector<RudderItem>& all,
                                   const std::string& query,
                                   const FrecencyFn& frecency)
{
    auto scored = std::vector<ScoredItem> {};

    auto add = [&](int match, RudderItem item)
    {
        auto bonus = frecency ? frecencyBonus(frecency(frecencyKey(item))) : 0.0;
        auto penalty = item.running ? 0.0 : closedPenalty;
        scored.push_back({match + bonus - penalty, match, std::move(item)});
    };

    for (auto& app: all)
    {
        if (app.kind == "favourite")
        {
            if (auto score = grazer::fuzzyScore(query, favouriteText(app)))
                add(*score, app);

            continue;
        }

        if (auto score = grazer::fuzzyScore(query, app.title))
            add(*score, app);

        // A context matches on its app's name, its title and url, its
        // directory and its subtitle, so "grazer github" finds the GitHub tab
        // in Grazer and "grazer" the pane sitting in ~/projects/Grazer.
        for (auto& context: app.contexts)
            if (auto score = contextScore(query, app, context))
                add(*score, contextRow(app, context));
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
    if (!query.empty() || items.size() < 2)
        return 0;

    return items.front().id == frontAppId ? 1 : 0;
}
} // namespace rudder
