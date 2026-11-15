#include "LearnedPickStore.h"

#include "Frecency.h"

#include <FrecencySearch/MatchLevel.h>

namespace rudder
{
namespace
{
std::string calculateEntryId(const std::string& query, const std::string& destinationKey)
{
    return query + "\n" + destinationKey;
}

frecencysearch::LearnedPick toLearnedPick(const LearnedPickEntry& entry)
{
    return {entry.query, entry.destinationKey, entry.count, entry.at};
}
} // namespace

LearnedPickStore::LearnedPickStore(const emberstore::Database& db)
    : entries(db.collection<LearnedPickEntry>("learnedPicks"))
{
    forgetExpiredPicks(FrecencyStore::nowMs());
}

void LearnedPickStore::recordPick(const std::string& query,
                                  const std::string& destinationKey,
                                  std::int64_t nowMs)
{
    auto normalisedQuery = frecencysearch::normaliseQuery(query);

    if (normalisedQuery.empty())
        return;

    auto id = calculateEntryId(normalisedQuery, destinationKey);
    auto previous = frecencysearch::LearnedPick {normalisedQuery, destinationKey};

    if (auto entry = entries.get(id))
        previous = toLearnedPick(*entry);

    auto updated = frecencysearch::addPickTo(previous, nowMs);
    entries.set(id, {updated.query, updated.destinationKey, updated.count, updated.atMs});
    forgetExpiredPicks(nowMs);
}

std::vector<frecencysearch::LearnedPick>
    LearnedPickStore::findPicksForQuery(const std::string& query, std::int64_t nowMs)
{
    auto normalisedQuery = frecencysearch::normaliseQuery(query);
    return frecencysearch::findLearnedPicksForQuery(getAllPicks(), normalisedQuery, nowMs);
}

void LearnedPickStore::forgetExpiredPicks(std::int64_t nowMs)
{
    for (auto& pick: frecencysearch::findLearnedPicksToForget(getAllPicks(), nowMs))
        entries.remove(calculateEntryId(pick.query, pick.destinationKey));
}

void LearnedPickStore::forgetDestination(const std::string& destinationKey)
{
    for (auto& pick: getAllPicks())
        if (pick.destinationKey == destinationKey)
            entries.remove(calculateEntryId(pick.query, pick.destinationKey));
}

std::vector<frecencysearch::LearnedPick> LearnedPickStore::getAllPicks()
{
    auto picks = std::vector<frecencysearch::LearnedPick> {};

    for (auto& entry: entries.values())
        picks.push_back(toLearnedPick(entry));

    return picks;
}
} // namespace rudder
