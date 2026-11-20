#pragma once

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <map>
#include <string>

namespace rudder
{
struct Stamp
{
    std::int64_t at = 0;

    MIRO_REFLECT(at)
};

// Recency stamps for palette ordering, keyed by normalized URL. Written on
// every real use (palette pick, tab cycle, active-tab navigation), so give it
// an Atomic (not Durable) database -- losing the last stamps to a power cut
// is harmless. Reads come from a copy in memory: Rudder is the file's only
// writer, and reading the store checks the file on disk on every call, which
// a re-rank does once per row.
struct MruStore
{
    explicit MruStore(const emberstore::Database& db);

    void touch(const std::string& normalizedUrl, std::int64_t atMs = nowMs());

    std::int64_t lastUsed(const std::string& normalizedUrl);

    // Drops the stamp: what it was for is gone.
    void forget(const std::string& normalizedUrl);

    // Drops the oldest stamps once the collection outgrows maxEntries.
    void prune();

    static std::int64_t nowMs();

    static constexpr std::size_t maxEntries = 500;

    emberstore::Collection<Stamp> stamps;

private:
    void loadStampsIntoMemory();

    std::map<std::string, std::int64_t> stampsInMemory;
};
} // namespace rudder
