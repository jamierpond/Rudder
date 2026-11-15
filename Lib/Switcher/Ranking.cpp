#include "Ranking.h"

#include <FrecencySearch/RowOrder.h>

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
    row.repo = context.repo;
    row.contextPath = context.path;

    // A place that isn't open yet reads like a closed app and, like one, is
    // never previewed: activating it may open something.
    row.running = context.open;
    return row;
}

std::string lastComponent(std::string path)
{
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    auto slash = path.find_last_of('/');

    if (slash == std::string::npos)
        return path;

    return path.substr(slash + 1);
}
} // namespace

namespace
{
std::string stableKey(const std::string& appId,
                      const std::string& repo,
                      const std::string& branch,
                      const std::string& path,
                      const std::string& id)
{
    if (!repo.empty() && !branch.empty())
        return appId + "/repo:" + repo + "#branch:" + branch;

    if (!path.empty())
        return appId + "/path:" + path;

    return appId + "/" + id;
}
} // namespace

std::string contextKey(const std::string& appId, const RudderContext& context)
{
    return stableKey(appId, context.repo, context.branch, context.path, context.id);
}

std::vector<std::string> goneContextKeys(const std::string& appId,
                                         const std::vector<RudderContext>& before,
                                         const std::vector<RudderContext>& after)
{
    auto ids = std::unordered_set<std::string> {};
    auto kept = std::unordered_set<std::string> {};

    for (auto& context: after)
    {
        ids.insert(context.id);
        kept.insert(contextKey(appId, context));
    }

    auto gone = std::vector<std::string> {};

    for (auto& context: before)
        if (!ids.contains(context.id))
            if (auto key = contextKey(appId, context); kept.insert(key).second)
                gone.push_back(std::move(key));

    return gone;
}

std::vector<std::string> idOnlyContextKeys(const std::string& appId,
                                           const std::vector<RudderContext>& contexts)
{
    auto seen = std::unordered_set<std::string> {};
    auto keys = std::vector<std::string> {};

    for (auto& context: contexts)
        if (context.path.empty() && (context.repo.empty() || context.branch.empty()))
            if (auto key = contextKey(appId, context); seen.insert(key).second)
                keys.push_back(std::move(key));

    return keys;
}

std::string frecencyKey(const RudderItem& item)
{
    if (item.kind == "context" || item.kind == "window")
        return stableKey(item.appId, item.repo, item.branch, item.contextPath, item.contextId);

    if (item.kind != "favourite")
        return item.id;

    auto target = !item.url.empty()    ? item.url
                  : !item.path.empty() ? item.path
                  : !item.appId.empty() ? item.appId
                                        : item.title;
    return "fav:" + target;
}

bool pickMoves(const RudderItem& item,
               const std::string& frontAppId,
               const std::vector<RudderContext>& frontContexts)
{
    if (frontAppId.empty() || item.appId != frontAppId)
        return true;

    if (item.kind == "app")
        return false;

    if (item.kind != "context")
        return true;

    return std::none_of(frontContexts.begin(),
                        frontContexts.end(),
                        [&](const RudderContext& context)
                        { return context.focused && context.id == item.contextId; });
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

namespace
{

bool isContextRow(const RudderItem& item)
{
    return item.kind == "context" || item.kind == "window";
}

// An app whose panes or tabs are listed is already there through them.
void dropAppRowsShadowedByTheirContexts(std::vector<RudderItem>& rows)
{
    auto appsWithContexts = std::unordered_set<std::string> {};

    for (auto& row: rows)
        if (isContextRow(row))
            appsWithContexts.insert(row.appId);

    std::erase_if(rows,
                  [&](const RudderItem& row)
                  {
                      return row.kind == "app"
                             && appsWithContexts.contains(row.id);
                  });
}
} // namespace

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

    dropAppRowsShadowedByTheirContexts(browsing);
    return browsing;
}

namespace
{
using frecencysearch::MatchLevel;

frecencysearch::SearchableTexts favouriteTexts(const RudderItem& favourite)
{
    auto texts = frecencysearch::SearchableTexts {};
    texts.names.push_back(favourite.title);
    texts.names.insert(texts.names.end(), favourite.keywords.begin(), favourite.keywords.end());
    texts.details = {favourite.subtitle, frecencysearch::removeUrlScheme(favourite.url)};
    return texts;
}

frecencysearch::SearchableTexts contextTexts(const RudderContext& context)
{
    auto texts = frecencysearch::SearchableTexts {};
    texts.names = {context.name, context.title, context.branch, lastComponent(context.path)};
    texts.details = {context.subtitle, context.path, frecencysearch::removeUrlScheme(context.url)};
    return texts;
}

// The query after the app's name, when it starts with it, or with its first
// word starting the name, and has more: "grazer github" and "iterm readme"
// leave "github" and "readme" for the app's tabs and windows.
std::optional<std::string> findQueryAfterAppName(const std::string& normalisedQuery,
                                                 const RudderItem& app)
{
    auto appName = frecencysearch::normaliseQuery(app.title);
    auto firstSpace = normalisedQuery.find(' ');
    auto firstWord = normalisedQuery.substr(0, firstSpace);
    auto namePrefixLength = std::size_t {0};

    if (!appName.empty() && normalisedQuery.starts_with(appName))
        namePrefixLength = appName.size();
    else if (firstSpace != std::string::npos && appName.starts_with(firstWord))
        namePrefixLength = firstWord.size();

    if (namePrefixLength == 0)
        return std::nullopt;

    auto rest = frecencysearch::normaliseQuery(normalisedQuery.substr(namePrefixLength));

    if (rest.empty())
        return std::nullopt;

    return rest;
}

std::optional<MatchLevel> findContextMatchLevel(const std::string& normalisedQuery,
                                                const RudderItem& app,
                                                const RudderContext& context)
{
    auto texts = contextTexts(context);
    auto level = frecencysearch::findMatchLevel(normalisedQuery, texts);

    if (auto rest = findQueryAfterAppName(normalisedQuery, app))
    {
        auto levelAfterAppName = frecencysearch::findMatchLevel(*rest, texts);

        if (frecencysearch::isBetterMatchLevel(levelAfterAppName, level))
            level = levelAfterAppName;
    }

    return level;
}

bool isWindowNamedOtherThanItsApp(const RudderItem& app, const RudderContext& context)
{
    return context.kind == "window"
           && frecencysearch::normaliseQuery(context.title)
                  != frecencysearch::normaliseQuery(app.title);
}

constexpr std::size_t windowsNeededToListThem = 2;

bool shouldListWindows(const RudderItem& app)
{
    auto namedWindows = std::count_if(app.contexts.begin(),
                                      app.contexts.end(),
                                      [&](const RudderContext& context)
                                      { return isWindowNamedOtherThanItsApp(app, context); });
    return (std::size_t) namedWindows >= windowsNeededToListThem;
}

bool isListedContext(const RudderItem& app, const RudderContext& context)
{
    if (context.kind != "window")
        return true;

    return isWindowNamedOtherThanItsApp(app, context) && shouldListWindows(app);
}

struct CandidateRow
{
    MatchLevel level = MatchLevel::nameHasQueryLettersInOrder;
    RudderItem item;
    bool isFocused = false;
};

// Of two rows for one destination (panes on the same branch), the one to
// open: the focused one, else an open one.
bool isBetterRowForDestination(const CandidateRow& candidate, const CandidateRow& current)
{
    if (candidate.isFocused != current.isFocused)
        return candidate.isFocused;

    return candidate.item.running && !current.item.running;
}

void addCandidateRow(std::vector<CandidateRow>& rows, CandidateRow candidate)
{
    auto key = frecencyKey(candidate.item);
    auto same = std::find_if(rows.begin(),
                             rows.end(),
                             [&](const CandidateRow& row) { return frecencyKey(row.item) == key; });

    if (same == rows.end())
    {
        rows.push_back(std::move(candidate));
        return;
    }

    auto bestLevel = std::min(same->level, candidate.level);

    if (isBetterRowForDestination(candidate, *same))
        *same = std::move(candidate);

    same->level = bestLevel;
}

std::vector<CandidateRow> findCandidateRows(const std::vector<RudderItem>& all,
                                            const std::string& normalisedQuery)
{
    auto rows = std::vector<CandidateRow> {};

    for (auto& app: all)
    {
        if (app.kind == "favourite")
        {
            if (auto level = frecencysearch::findMatchLevel(normalisedQuery, favouriteTexts(app)))
                addCandidateRow(rows, {*level, app});

            continue;
        }

        if (auto level = frecencysearch::findNameMatchLevel(normalisedQuery, app.title))
            addCandidateRow(rows, {*level, app});

        for (auto& context: app.contexts)
        {
            if (!isListedContext(app, context))
                continue;

            if (auto level = findContextMatchLevel(normalisedQuery, app, context))
                addCandidateRow(rows, {*level, contextRow(app, context), context.focused});
        }
    }

    return rows;
}

frecencysearch::RowRankingFacts calculateRankingFacts(const CandidateRow& row,
                                                      const SearchUsage& usage)
{
    auto key = frecencyKey(row.item);
    auto facts = frecencysearch::RowRankingFacts {};
    facts.level = row.level;
    facts.isOpen = row.item.running;

    if (usage.usage)
        facts.usage = usage.usage(key);

    for (auto& pick: usage.learnedPicks)
    {
        if (pick.destinationKey != key)
            continue;

        facts.learnedPicks = pick.count;
        facts.learnedAtMs = pick.atMs;
    }

    return facts;
}
} // namespace

std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query,
                                  const SearchUsage& usage,
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

    for (auto& matched: matchItems(all, query, usage))
        ranked.push_back(std::move(matched.item));

    return ranked;
}

std::vector<MatchedItem> matchItems(const std::vector<RudderItem>& all,
                                    const std::string& query,
                                    const SearchUsage& usage)
{
    auto normalisedQuery = frecencysearch::normaliseQuery(query);

    if (normalisedQuery.empty())
        return {};

    struct RankedRow
    {
        frecencysearch::RowRankingFacts facts;
        CandidateRow row;
    };

    auto rankedRows = std::vector<RankedRow> {};

    for (auto& row: findCandidateRows(all, normalisedQuery))
        rankedRows.push_back({calculateRankingFacts(row, usage), std::move(row)});

    std::stable_sort(rankedRows.begin(),
                     rankedRows.end(),
                     [](const RankedRow& first, const RankedRow& second)
                     { return frecencysearch::shouldRankBefore(first.facts, second.facts); });

    auto matched = std::vector<MatchedItem> {};

    for (auto& ranked: rankedRows)
        matched.push_back({ranked.row.level, std::move(ranked.row.item)});

    return matched;
}

bool isPreviewable(const RudderItem& item, bool runningNow)
{
    return item.kind != "web" && item.running && runningNow;
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
