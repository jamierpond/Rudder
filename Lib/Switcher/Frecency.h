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

    // The key's score decayed to atMs; 0 for a key never picked.
    double frecency(const std::string& key, std::int64_t atMs = nowMs());

    // Drops the lowest scores once the collection outgrows maxEntries.
    void prune();

    static std::int64_t nowMs();

    // `score` decayed from `fromMs` to `toMs`.
    static double decayed(double score, std::int64_t fromMs, std::int64_t toMs);

    static constexpr std::int64_t halfLifeMs = 7LL * 24 * 60 * 60 * 1000;
    static constexpr std::size_t maxEntries = 500;

    emberstore::Collection<FrecencyEntry> entries;
};
} // namespace rudder
