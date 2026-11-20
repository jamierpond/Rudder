#pragma once

#include "AppOrders.h"
#include "Items.h"

#include <FrecencySearch/LearnedPicks.h>
#include <FrecencySearch/MatchLevel.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rudder
{
// A row's last-used stamp by its frecencyKey; 0 if never used.
using LastUsedFn = std::function<std::int64_t(const std::string& key)>;

// A row's frecency (FrecencyStore::frecency) by its frecencyKey; 0 if never
// picked.
using FrecencyFn = std::function<double(const std::string& key)>;

// What a context is remembered by -- its frecency, its recency, its rename --
// stable across shells and restarts, where a provider's id isn't (CowTerm's
// pane ids are new for every shell): appId + "/repo:" + repo + "#branch:" +
// branch when it has both, else appId + "/path:" + its directory, else
// appId + "/" + its id. A live pane and the unopened place on the same branch
// of the same repository share it, so their uses add up.
std::string contextKey(const std::string& appId, const RudderContext& context);

// The row for one of `app`'s contexts: renamed ones titled by their name, a
// place that isn't open yet not running.
RudderItem makeContextRow(const RudderItem& app, const RudderContext& context);

// The contextKeys of the contexts in `before` that `after` has no context
// with the same id for: what an app's push dropped -- a tab or pane closed
// (from Rudder or in the app), or expired. A key some context in `after`
// still carries (another pane on the branch, its unopened place) isn't
// gone, and neither is the old key of a context that only moved (a pane
// that cd'd or checked out another branch keeps its id). Each key once.
std::vector<std::string> goneContextKeys(const std::string& appId,
                                         const std::vector<RudderContext>& before,
                                         const std::vector<RudderContext>& after);

// The contextKeys among `contexts` that are only the provider's id (no
// repository and branch, no path). Those ids mean nothing once the app's connection ends:
// Grazer numbers its tabs from 0 again each launch, so tab "3" after a
// restart is another page.
std::vector<std::string> idOnlyContextKeys(const std::string& appId,
                                           const std::vector<RudderContext>& contexts);

// What a row's frecency (and recency) is stored under: its id, except a
// favourite's, which is its index in rudder.json and moves when the list is
// edited -- a favourite is keyed by what it opens instead -- and a context
// row's, which is its contextKey.
std::string frecencyKey(const RudderItem& item);

// Whether picking `item` takes the user anywhere: not when it is the app
// the panel was summoned over (`frontAppId`), or the context that app is
// showing (focused among `frontContexts`). Enter there only closes the panel
// -- the user is already where they are -- so it isn't counted as a pick.
bool pickMoves(const RudderItem& item,
               const std::string& frontAppId,
               const std::vector<RudderContext>& frontContexts);

// Most-recently-used first, each row's stamp looked up by its frecencyKey.
// Stable, so rows never used keep the order they came in: pass the running
// apps in the OS's order with the favourites after them, and running apps
// never used stay in OS order above favourites never used.
std::vector<RudderItem> orderByRecency(std::vector<RudderItem> apps,
                                       const LastUsedFn& lastUsed);

// Running apps (and any favourites among them) first, then every installed
// app that isn't running (matched by bundle id, so an app appears once; a
// favourite that opens an app doesn't hide it). Both halves keep their order, so call
// with each already by recency.
std::vector<RudderItem> mergeInstalled(std::vector<RudderItem> running,
                                       const std::vector<RudderItem>& installed);

// What the empty query's list needs to show recent contexts among the apps.
struct Recency
{
    // Each row's stamp by its frecencyKey (the app summoned over: highest).
    LastUsedFn lastUsed;
    std::int64_t nowMs = 0;

    // The app the panel was summoned over: its focused context is where the
    // user already is, so it isn't listed.
    std::string frontAppId;

    // The order each app that handed Rudder its ranking materials gives its
    // rows with nothing typed.
    AppOrderedContexts appOrders;
};

// The empty query lists a context used within this long, and at most
// recentContextLimit of them: a working day and the evening after, so this
// morning's panes and yesterday afternoon's are there and last week's
// aren't, and few enough that the apps stay on the first screen (the panel
// shows about nine rows).
constexpr std::int64_t recentContextWindowMs = 36LL * 60 * 60 * 1000;
constexpr std::size_t recentContextLimit = 6;

// The empty query's list: the running apps and the favourites, plus the
// contexts used lately (recentContextWindowMs, at most recentContextLimit,
// one row per contextKey, an open one over a place), all by recency. A
// context used at the same moment as its app sits just above it.
std::vector<RudderItem> browseItems(const std::vector<RudderItem>& all,
                                    const Recency& recency);

// What orders a typed query's rows besides the match: each row's decayed
// usage by its frecencyKey, and the picks learned for the typed text
// (LearnedPickStore::findPicksForQuery).
struct SearchUsage
{
    FrecencyFn usage;
    std::vector<frecencysearch::LearnedPick> learnedPicks;

    // Each app that handed Rudder its ranking materials: its rows for the
    // typed text in its own order. They are that app's rows, in place of the
    // contexts it pushed.
    AppOrderedContexts appOrders;
};

// Each app's rows among `rows` that appear in its order in `appOrders` are put
// in that order, in the places those rows already hold: Rudder decides where
// an app's rows sit among other apps' rows and its own, the app decides
// their order among themselves.
std::vector<RudderItem> keepEachAppsOwnOrder(std::vector<RudderItem> rows,
                                             const AppOrderedContexts& appOrders);

// The list for the current query. Empty query: browseItems with `recency`,
// or without it the running apps and the favourites in `all`'s order.
// Otherwise every app, favourite and context that matches, one row per
// frecencyKey, in frecencysearch::shouldRankBefore order; ties keep `all`'s
// order. A context also matches on the rest of a query that starts with its
// app's name ("grazer github"). An app's windows are rows only when at least
// two are titled other than the app.
std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query,
                                  const SearchUsage& usage = {},
                                  const Recency& recency = {});

struct MatchedItem
{
    frecencysearch::MatchLevel level = frecencysearch::MatchLevel::nameHasQueryLettersInOrder;
    RudderItem item;
};

// rankItems for a non-empty query, keeping each row's match level.
std::vector<MatchedItem> matchItems(const std::vector<RudderItem>& all,
                                    const std::string& query,
                                    const SearchUsage& usage = {});

// Whether the live preview shows a snapshot of a selection: only an app that
// is running at the moment of the call. A closed app, an app that quit since
// the list was built, or a web row shows nothing.
bool isPreviewable(const RudderItem& item, bool runningNow);

// Where the selection starts. With no query the first row is normally the app
// the panel was summoned over, so cmd-tab style it starts on the next one --
// Enter goes back to the previous app. Typing selects the best hit.
int initialSelection(const std::vector<RudderItem>& items,
                     const std::string& query,
                     const std::string& frontAppId);
} // namespace rudder
