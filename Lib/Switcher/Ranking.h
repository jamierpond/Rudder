#pragma once

#include "Items.h"

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

// Added to a row's match score for how often and how lately it was picked:
// k * log2(1 + frecencyScale * frecency), at most frecencyCap. The cap is
// one consecutive letter's worth in fuzzyScore (8, against 1 for a scattered
// one), so frecency reorders matches that are about as good but never lifts
// a weak match over a clearly better one.
//
// Rudder is the app switcher, so heavy use is hundreds of picks a day (the
// live store showed one every ~80 s), and a row picked n times a day settles
// near n / (1 - 2^(-1/3)) = 4.85 n frecency (FrecencyStore::halfLifeMs, 3
// days). The old curve, 2 * log2(1 + frecency), hit the cap at frecency 15
// -- three picks a day -- so every row the user actually lives in tied at 8
// and frecency told them apart no more.
//
// This one keeps the low end and stretches the top: k = 2/3 and scale 8
// make it about 2 + (2/3) log2(frecency) once frecency is past 1. One pick
// is worth 2.1 (as before: a near-tie flips on a single pick), three 3.1; a
// pick a day settles at 3.5, five a day 5.1, twenty 6.4, fifty 7.3, and the
// cap comes only at frecency 512, about 105 picks a day. Every doubling of use
// is worth the same 2/3 of a point, so the busiest rows keep their order at
// any volume a person reaches.
constexpr auto frecencyK = 2.0 / 3.0;
constexpr auto frecencyScale = 8.0;
constexpr auto frecencyCap = 8.0;

double frecencyBonus(double frecency);

// Taken off a row that isn't open -- an app that isn't running, a place
// (a branch with no pane on it yet) -- so an open row wins on an about-equal
// match. The same one letter run as frecencyCap: a much better match on a
// closed row still wins, and a closed row picked often only evens out.
constexpr auto closedPenalty = 8.0;

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

// The list for the current query. Empty query: browseItems with `recency`,
// or without it the running apps and the favourites in `all`'s order (by
// recency) -- the switcher, not a launcher of hundreds. Otherwise fuzzy-rank the app titles
// together with every app's contexts (the start of the query may name the
// app, the rest matches the context's branch and the name the user gave it,
// then its title and url, then its directory's
// name, subtitle and path; a context matched by its app's name alone ranks
// below the app), plus each row's frecencyBonus, each
// context flattened into a row of its own (kind "context" or "window") so a
// tab, a session or a window is one Enter away. Only real
// hits are kept; on equal scores running apps beat closed ones, and ties
// after that keep the recency order.
// No `frecency` ranks by match alone.
std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query,
                                  const FrecencyFn& frecency = {},
                                  const Recency& recency = {});

struct ScoredItem
{
    // match plus the row's frecencyBonus: what the rows are sorted by.
    double score = 0;
    // The fuzzy match alone.
    int match = 0;
    RudderItem item;
};

// rankItems for a non-empty query, keeping each row's scores, best first.
std::vector<ScoredItem> scoreItems(const std::vector<RudderItem>& all,
                                   const std::string& query,
                                   const FrecencyFn& frecency = {});

// What the live preview does for a selection. Only ever brings forward an
// app that is running at the moment of the call; anything else -- a closed
// app, an app that quit since the list was built, a web row -- is left alone. The
// preview never launches, opens or activates something that isn't running.
// restoreOrigin is only for the row of the app the panel was summoned over;
// that app's windows and contexts preview like any other app's.
enum class PreviewAction
{
    none,
    bringForward,
    restoreOrigin
};

PreviewAction previewActionFor(const RudderItem& item,
                               bool runningNow,
                               const std::string& originAppId);

// Where the selection starts. With no query the first row is normally the app
// the panel was summoned over, so cmd-tab style it starts on the next one --
// Enter goes back to the previous app. Typing selects the best hit.
int initialSelection(const std::vector<RudderItem>& items,
                     const std::string& query,
                     const std::string& frontAppId);
} // namespace rudder
