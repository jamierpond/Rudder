// Replays queries against the rows Rudder last saved (panel-rows.json) with
// the live frecency and MRU stores, printing each hit's score. Point it at a
// copy of Rudder's data folder: `just ranking-probe <query>...` makes one.
#include <Frecency.h>
#include <Mru.h>
#include <Ranking.h>

#include <cstdio>
#include <string>

using namespace rudder;

int main(int argc, char** argv)
{
    constexpr auto shownRowLimit = 8;

    if (argc < 3)
    {
        std::fprintf(stderr, "usage: RankingProbe <data folder> <query>...\n");
        return 2;
    }

    auto database = emberstore::Database {eacp::FilePath {argv[1]}, emberstore::Durability::Atomic};
    auto snapshots = database.collection<PanelRowsSnapshot>("panel-rows");
    auto snapshot = snapshots.get("latest");

    if (!snapshot)
    {
        std::fprintf(stderr, "no panel-rows.json in %s\n", argv[1]);
        return 1;
    }

    auto frecencyStore = FrecencyStore {database};
    auto mru = grazer::MruStore {database};
    auto now = FrecencyStore::nowMs();
    auto frecency = [&](const std::string& key) { return frecencyStore.frecency(key, now); };
    auto lastUsed = [&](const std::string& key)
    { return key == snapshot->frontAppId ? INT64_MAX : mru.lastUsed(key); };
    auto rows = orderByRecency(snapshot->items, lastUsed);

    std::printf("front app: %s\n", snapshot->frontAppId.c_str());

    for (auto argument = 2; argument < argc; ++argument)
    {
        auto query = std::string {argv[argument]};
        std::printf("\n\"%s\"\n", query.c_str());

        auto shown = 0;

        for (auto& scored: scoreItems(rows, query, frecency, snapshot->frontAppId))
        {
            if (shown++ == shownRowLimit)
                break;

            std::printf("  %6.1f  match %3d  %-7s %s  |  %s\n",
                        scored.score,
                        scored.match,
                        scored.item.kind.c_str(),
                        scored.item.title.c_str(),
                        scored.item.subtitle.c_str());
        }
    }

    return 0;
}
