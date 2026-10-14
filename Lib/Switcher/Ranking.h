#pragma once

#include "Items.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rudder
{
using LastUsedFn = std::function<std::int64_t(const std::string& appId)>;

// The panel's base list: running apps, most-recently-used first. Stable, so
// apps never used keep the order the OS listed them in.
std::vector<RudderItem> orderByRecency(std::vector<RudderItem> apps,
                                       const LastUsedFn& lastUsed);

// Running apps first, then every installed app that isn't running (matched by
// bundle id, so an app appears once). Both halves keep their order, so call
// with each already by recency.
std::vector<RudderItem> mergeInstalled(std::vector<RudderItem> running,
                                       const std::vector<RudderItem>& installed);

// The list for the current query. Empty query: favourites (in list order, as
// `all` has them first) and the running apps -- the switcher, not a launcher
// of hundreds. Otherwise fuzzy-rank the app titles
// together with every app's contexts (the start of the query may name the
// app, the rest matches the context's title and url, then its directory's
// name, subtitle and path; a context matched by its app's name alone ranks
// below the app), each
// context flattened into a row of its own (kind "context" or "window") so a
// tab, a session or a window is one Enter away. Only real
// hits are kept; on equal scores running apps beat closed ones, and ties
// after that keep the recency order.
std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query);

struct ScoredItem
{
    int score = 0;
    RudderItem item;
};

// rankItems for a non-empty query, keeping each row's score (a favourite's
// includes favouriteBoost), best first.
std::vector<ScoredItem> scoreItems(const std::vector<RudderItem>& all,
                                   const std::string& query);

// What the live preview does for a selection. Only ever brings forward an
// app that is running at the moment of the call; anything else -- a closed
// app, an app that quit since the list was built, a web row -- is left alone. The
// preview never launches, opens or activates something that isn't running.
enum class PreviewAction
{
    none,
    bringForward,
    restoreOrigin
};

PreviewAction previewActionFor(const RudderItem& item,
                               bool runningNow,
                               const std::string& originAppId);

// Added to a favourite's match score: a favourite that matches at all beats
// an app that matches about as well.
constexpr auto favouriteBoost = 20;

// Where the selection starts. With no query the first app row is the app the
// panel was summoned over, so cmd-tab style it starts on the next one (below
// any favourites) -- Enter goes back to the previous app. Typing selects the
// best hit.
int initialSelection(const std::vector<RudderItem>& items,
                     const std::string& query,
                     const std::string& frontAppId);
} // namespace rudder
