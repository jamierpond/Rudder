#include "TestSupport.h"

#include <Frecency.h>

#include <NanoTest/NanoTest.h>

#include <cmath>

using namespace nano;
using namespace rudder;

namespace
{
constexpr auto week = FrecencyStore::halfLifeMs;

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
} // namespace

auto tUnknownKeyIsZero = test("Frecency/an untouched key reports zero") = []
{
    auto db = testing::freshDatabase("frecency-zero");
    auto store = FrecencyStore {db};

    check(store.frecency("com.apple.Safari", 1000) == 0);
};

auto tTouchesAdd = test("Frecency/each pick adds one") = []
{
    auto db = testing::freshDatabase("frecency-add");
    auto store = FrecencyStore {db};

    store.touch("com.apple.Safari", 1000);
    store.touch("com.apple.Safari", 1000);

    check(near(store.frecency("com.apple.Safari", 1000), 2));
};

auto tHalvesPerHalfLife = test("Frecency/the score halves every half-life") = []
{
    auto db = testing::freshDatabase("frecency-decay");
    auto store = FrecencyStore {db};

    store.touch("com.apple.Safari", 0);
    store.touch("com.apple.Safari", 0);

    check(near(store.frecency("com.apple.Safari", week), 1));
    check(near(store.frecency("com.apple.Safari", 2 * week), 0.5));
};

auto tTouchDecaysFirst = test("Frecency/a pick decays the old score before adding") = []
{
    auto db = testing::freshDatabase("frecency-touch-decay");
    auto store = FrecencyStore {db};

    store.touch("com.apple.Safari", 0);
    store.touch("com.apple.Safari", week);

    check(near(store.frecency("com.apple.Safari", week), 1.5));
    check(store.entries.get("com.apple.Safari")->at == week);
};

auto tFutureStampDoesntGrow = test("Frecency/a stamp from the future doesn't grow") = []
{ check(near(FrecencyStore::decayed(3, 1000, 0), 3)); };

auto tPruneKeepsHighest = test("Frecency/prune drops the lowest scores") = []
{
    auto db = testing::freshDatabase("frecency-prune");
    auto store = FrecencyStore {db};
    auto now = FrecencyStore::nowMs();

    // Older picks have decayed further, so key 0 scores lowest.
    for (std::size_t i = 0; i < FrecencyStore::maxEntries + 10; ++i)
        store.touch("row-" + std::to_string(i), now - week + (std::int64_t) i * 1000);

    store.prune();

    check(store.entries.size() == FrecencyStore::maxEntries);
    check(store.frecency("row-0") == 0);
    check(store.frecency("row-" + std::to_string(FrecencyStore::maxEntries + 9)) > 0);
};

auto tWeightedTouch = test("Frecency/a weighted touch adds its fraction") = []
{
    auto db = testing::freshDatabase("frecency-weight");
    auto store = FrecencyStore {db};

    store.touch("com.eacp.cowterm/branch:main", 1000, 0.25);
    store.touch("com.eacp.cowterm/branch:main", 1000);

    check(near(store.frecency("com.eacp.cowterm/branch:main", 1000), 1.25));
};

auto tFrecencyForgetDrops = test("Frecency/forget drops a key's score, and a later pick starts afresh")
    = []
{
    auto db = testing::freshDatabase("frecency-forget");
    auto store = FrecencyStore {db};

    store.touch("audio.pond.grazer/3", 1000);
    store.touch("audio.pond.grazer/3", 1000);
    store.touch("audio.pond.grazer/4", 1000);
    store.forget("audio.pond.grazer/3");

    check(store.frecency("audio.pond.grazer/3", 1000) == 0);
    check(near(store.frecency("audio.pond.grazer/4", 1000), 1));

    store.touch("audio.pond.grazer/3", 1000);
    check(near(store.frecency("audio.pond.grazer/3", 1000), 1));
};
