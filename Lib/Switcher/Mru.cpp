#include "Mru.h"

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

namespace grazer
{
MruStore::MruStore(const emberstore::Database& db)
    : stamps(db.collection<Stamp>("mru"))
{
    loadStampsIntoMemory();
    prune();
}

void MruStore::loadStampsIntoMemory()
{
    for (auto& id: stamps.ids())
        if (auto stamp = stamps.get(id))
            stampsInMemory[id] = stamp->at;
}

void MruStore::touch(const std::string& normalizedUrl, std::int64_t atMs)
{
    stamps.doc(normalizedUrl).set({atMs});
    stampsInMemory[normalizedUrl] = atMs;
}

std::int64_t MruStore::lastUsed(const std::string& normalizedUrl)
{
    if (auto stamp = stampsInMemory.find(normalizedUrl); stamp != stampsInMemory.end())
        return stamp->second;

    return 0;
}

void MruStore::forget(const std::string& normalizedUrl)
{
    stamps.remove(normalizedUrl);
    stampsInMemory.erase(normalizedUrl);
}

void MruStore::prune()
{
    if (stamps.size() <= maxEntries)
        return;

    auto aged = std::vector<std::pair<std::int64_t, std::string>> {};

    for (auto& id: stamps.ids())
        aged.emplace_back(lastUsed(id), id);

    std::sort(aged.begin(), aged.end());

    auto excess = aged.size() - maxEntries;

    for (std::size_t i = 0; i < excess; ++i)
        forget(aged[i].second);
}

std::int64_t MruStore::nowMs()
{
    using namespace std::chrono;
    return (std::int64_t) duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}
} // namespace grazer
