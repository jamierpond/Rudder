#include "Frecency.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>
#include <vector>

namespace rudder
{
FrecencyStore::FrecencyStore(const emberstore::Database& db)
    : entries(db.collection<FrecencyEntry>("frecency"))
{
    prune();
}

void FrecencyStore::touch(const std::string& key, std::int64_t atMs, double weight)
{
    entries.doc(key).set({frecency(key, atMs) + weight, atMs});
}

double FrecencyStore::frecency(const std::string& key, std::int64_t atMs)
{
    if (auto entry = entries.get(key))
        return decayed(entry->score, entry->at, atMs);

    return 0;
}

void FrecencyStore::prune()
{
    if (entries.size() <= maxEntries)
        return;

    auto now = nowMs();
    auto ranked = std::vector<std::pair<double, std::string>> {};

    for (auto& id: entries.ids())
        ranked.push_back({frecency(id, now), id});

    std::sort(ranked.begin(), ranked.end());

    auto excess = ranked.size() - maxEntries;

    for (std::size_t i = 0; i < excess; ++i)
        entries.remove(ranked[i].second);
}

std::int64_t FrecencyStore::nowMs()
{
    using namespace std::chrono;
    return (std::int64_t) duration_cast<milliseconds>(
               system_clock::now().time_since_epoch())
        .count();
}

double FrecencyStore::decayed(double score, std::int64_t fromMs, std::int64_t toMs)
{
    // A stamp from the future (clock moved back) doesn't grow the score.
    auto elapsed = std::max<std::int64_t>(toMs - fromMs, 0);
    return score * std::exp2(-(double) elapsed / (double) halfLifeMs);
}
} // namespace rudder
