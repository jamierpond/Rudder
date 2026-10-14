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

// The list for the current query. Empty query: `all` unchanged. Otherwise
// fuzzy-rank the app titles together with every app's contexts, each context
// flattened into a row of its own (kind "context") so a tab or session is
// one Enter away. Only real hits are kept; ties keep the recency order.
std::vector<RudderItem> rankItems(const std::vector<RudderItem>& all,
                                  const std::string& query);

// Where the selection starts. With no query the top row is the app the panel
// was summoned over, so cmd-tab style it starts on the next one -- Enter
// goes back to the previous app. Typing selects the best hit.
int initialSelection(const std::vector<RudderItem>& items,
                     const std::string& query,
                     const std::string& frontAppId);
} // namespace rudder
