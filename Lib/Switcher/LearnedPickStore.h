#pragma once

#include <FrecencySearch/LearnedPicks.h>
#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rudder
{
struct LearnedPickEntry
{
    std::string query;
    std::string destinationKey;
    double count = 0;
    std::int64_t at = 0;

    MIRO_REFLECT(query, destinationKey, count, at)
};

// Which row the user picked for each typed text (frecencysearch learned
// picks), so the same text finds it first next time. Give it an Atomic
// database: losing the last picks to a power cut is harmless.
struct LearnedPickStore
{
    explicit LearnedPickStore(const emberstore::Database& db);

    void recordPick(const std::string& query, const std::string& destinationKey, std::int64_t nowMs);

    std::vector<frecencysearch::LearnedPick> findPicksForQuery(const std::string& query,
                                                               std::int64_t nowMs);

    void forgetExpiredPicks(std::int64_t nowMs);

    void forgetDestination(const std::string& destinationKey);

    std::vector<frecencysearch::LearnedPick> getAllPicks();

    emberstore::Collection<LearnedPickEntry> entries;
};
} // namespace rudder
