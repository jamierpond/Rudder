#include "LearnedPicks.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace frecencysearch
{
double calculateDecayedUsage(double usage, std::int64_t fromMs, std::int64_t toMs)
{
    auto elapsedMs = std::max<std::int64_t>(toMs - fromMs, 0);
    auto halfLives = (double) elapsedMs / (double) usageHalfLifeMs;
    return usage * std::exp2(-halfLives);
}

LearnedPick addPickTo(const LearnedPick& previous, std::int64_t nowMs)
{
    auto updated = previous;
    updated.count = calculateDecayedUsage(previous.count, previous.atMs, nowMs) + 1.0;
    updated.atMs = nowMs;
    return updated;
}

namespace
{
std::optional<std::string> findLongestLearnedQuery(const std::vector<LearnedPick>& learned,
                                                   const std::string& normalisedQuery)
{
    auto longest = std::optional<std::string> {};

    for (auto& pick: learned)
    {
        if (pick.query.empty() || !normalisedQuery.starts_with(pick.query))
            continue;

        if (!longest || pick.query.size() > longest->size())
            longest = pick.query;
    }

    return longest;
}
} // namespace

std::vector<LearnedPick> findLearnedPicksForQuery(const std::vector<LearnedPick>& learned,
                                                  const std::string& normalisedQuery,
                                                  std::int64_t nowMs)
{
    auto longestLearnedQuery = findLongestLearnedQuery(learned, normalisedQuery);
    auto found = std::vector<LearnedPick> {};

    if (!longestLearnedQuery)
        return found;

    for (auto& pick: learned)
    {
        if (pick.query != *longestLearnedQuery)
            continue;

        auto decayed = pick;
        decayed.count = calculateDecayedUsage(pick.count, pick.atMs, nowMs);
        found.push_back(decayed);
    }

    return found;
}

std::vector<LearnedPick> findLearnedPicksToForget(std::vector<LearnedPick> learned,
                                                  std::int64_t nowMs)
{
    std::sort(learned.begin(),
              learned.end(),
              [](const LearnedPick& first, const LearnedPick& second)
              { return first.atMs > second.atMs; });

    auto forgotten = std::vector<LearnedPick> {};

    for (std::size_t index = 0; index < learned.size(); ++index)
    {
        auto isExpired = nowMs - learned[index].atMs > learnedPickLifetimeMs;

        if (isExpired || index >= maximumLearnedPicks)
            forgotten.push_back(learned[index]);
    }

    return forgotten;
}
} // namespace frecencysearch
