#pragma once

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <string>

namespace rudder
{
struct FrecencyEntry
{
    double score = 0;
    std::int64_t at = 0;

    MIRO_REFLECT(score, at)
};

// How often and how lately each row was picked, keyed by frecencyKey. Every
// pick adds one (a focus Rudder only watched, a fraction: FocusTracker); the
// total halves every halfLife. Written on every pick, so
// give it an Atomic (not Durable) database -- losing the last picks to a
// power cut is harmless.
struct FrecencyStore
{
    explicit FrecencyStore(const emberstore::Database& db);

    // Adds `weight`: 1 for a pick, less for a use Rudder only saw (focusWeight).
    void touch(const std::string& key, std::int64_t atMs = nowMs(), double weight = 1.0);

    // Drops the key's score: its context is gone.
    void forget(const std::string& key);

    // The key's score decayed to atMs; 0 for a key never picked.
    double frecency(const std::string& key, std::int64_t atMs = nowMs());

    // Drops the lowest scores once the collection outgrows maxEntries.
    void prune();

    static std::int64_t nowMs();

    // `score` decayed from `fromMs` to `toMs`.
    static double decayed(double score, std::int64_t fromMs, std::int64_t toMs);

    // Three days: what the user works on now, not last week. Branches and
    // panes come and go within days: a pane picked 25 times a day for two
    // days and then left falls below one picked 5 times every day 4 days
    // later (FrecencySimulationTests), where at seven days it took almost 6.
    // An app used every working day still keeps 63% over a weekend.
    static constexpr std::int64_t halfLifeMs = 3LL * 24 * 60 * 60 * 1000;
    static constexpr std::size_t maxEntries = 500;

    emberstore::Collection<FrecencyEntry> entries;
};
} // namespace rudder
