#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace frecencysearch
{
// Usage halves every three days: what the user works on now, not last week.
constexpr std::int64_t usageHalfLifeMs = 3LL * 24 * 60 * 60 * 1000;

// A learned pick is forgotten four weeks after it was last made.
constexpr std::int64_t learnedPickLifetimeMs = 28LL * 24 * 60 * 60 * 1000;
constexpr std::size_t maximumLearnedPicks = 1000;

// A destination picked this often (decayed) for the typed text goes first:
// two picks, made within the last half day or so.
constexpr double learnedPicksToLeadTheList = 1.9;

// What the user picked after typing `query`: `destinationKey` names the
// row, `count` is its picks decayed to `atMs`, the last pick.
struct LearnedPick
{
    std::string query;
    std::string destinationKey;
    double count = 0;
    std::int64_t atMs = 0;
};

// `usage` from `fromMs` decayed to `toMs`. A stamp from the future (the
// clock moved back) doesn't grow it.
double calculateDecayedUsage(double usage, std::int64_t fromMs, std::int64_t toMs);

// `previous` (the same query and destination, count 0 if never picked) with
// one more pick at `nowMs`.
LearnedPick addPickTo(const LearnedPick& previous, std::int64_t nowMs);

// The picks learned for the text the user typed, counts decayed to `nowMs`:
// those for exactly that text, else those for the longest learned text it
// starts with. Empty when nothing fits.
std::vector<LearnedPick> findLearnedPicksForQuery(const std::vector<LearnedPick>& learned,
                                                  const std::string& normalisedQuery,
                                                  std::int64_t nowMs);

// The picks to drop: those not made for learnedPickLifetimeMs, then the
// oldest past maximumLearnedPicks.
std::vector<LearnedPick> findLearnedPicksToForget(std::vector<LearnedPick> learned,
                                                  std::int64_t nowMs);
} // namespace frecencysearch
