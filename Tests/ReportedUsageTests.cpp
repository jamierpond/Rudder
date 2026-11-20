#include <Frecency.h>
#include <Ranking.h>
#include <ReportedUsage.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <map>

using namespace nano;
using namespace rudder;

namespace
{
constexpr std::int64_t nowMs = 1'790'000'000'000LL;
constexpr std::int64_t hourMs = 60LL * 60 * 1000;
constexpr std::int64_t grazerUsageHalfLifeMs = frecencysearch::usageHalfLifeMs;
constexpr auto grazerId = "audio.pond.grazer";
constexpr auto tolerance = 1e-9;

bool near(double first, double second) { return std::abs(first - second) < tolerance; }

protocol::ContextUsage makeUseCount(const protocol::DecayingUseCount& useCount)
{
    auto usage = protocol::ContextUsage {};
    usage.useCount = useCount;
    return usage;
}

protocol::ContextUsage makeLastUsedAt(std::int64_t atMs)
{
    auto usage = protocol::ContextUsage {};
    usage.lastUsedAtMs = atMs;
    return usage;
}

RudderContext makeTab(const std::string& id, const std::string& title)
{
    return {id, title, "", "https://" + title + ".example"};
}

RudderItem makeGrazer(std::vector<RudderContext> tabs)
{
    auto grazer = RudderItem {};
    grazer.id = grazer.appId = grazerId;
    grazer.title = "Grazer";
    grazer.contexts = std::move(tabs);
    return grazer;
}

FrecencyFn makeOwnFrecency(std::map<std::string, double> byKey)
{
    return [byKey = std::move(byKey)](const std::string& key)
    {
        auto found = byKey.find(key);
        return found == byKey.end() ? 0.0 : found->second;
    };
}

std::vector<std::string> rankTitles(const std::vector<RudderItem>& all,
                                    const std::string& query,
                                    const FrecencyFn& usage)
{
    auto titles = std::vector<std::string> {};

    for (auto& row: rankItems(all, query, {usage, {}}))
        titles.push_back(row.title);

    return titles;
}
} // namespace

auto tNothingReported = test("ReportedUsage/with nothing reported a row keeps Rudder's own frecency") = []
{
    check(calculateRowFrecency(2.5, {}, nowMs) == 2.5);
    check(calculateRowFrecency(2.5, {protocol::ContextUsage {}}, nowMs) == 2.5);
};

auto tUseCountRedecayed = test("ReportedUsage/a use count is decayed on with the half-life it came with") = []
{
    auto usage = makeUseCount({4, nowMs - grazerUsageHalfLifeMs, grazerUsageHalfLifeMs});

    check(near(calculateStrongestReportedUse(usage, nowMs), 2));

    auto shortHalfLife = makeUseCount({4, nowMs - hourMs, hourMs});

    check(near(calculateStrongestReportedUse(shortHalfLife, nowMs), 2));
};

auto tReportedTime = test("ReportedUsage/a reported time is one use, decaying like Rudder's picks") = []
{
    check(near(calculateStrongestReportedUse(makeLastUsedAt(nowMs), nowMs), 1));
    check(near(calculateStrongestReportedUse(makeLastUsedAt(nowMs - reportedTimeHalfLifeMs), nowMs),
               0.5));

    auto typed = protocol::ContextUsage {};
    typed.lastTypedAtMs = nowMs;

    check(near(calculateStrongestReportedUse(typed, nowMs), 1));

    auto fileChange = protocol::ContextUsage {};
    fileChange.lastFileChangeAtMs = nowMs - reportedTimeHalfLifeMs;

    check(near(calculateStrongestReportedUse(fileChange, nowMs), 0.5));
};

auto tStrongestWins = test("ReportedUsage/the strongest sign of use counts, not the sum") = []
{
    auto usage = makeUseCount({4, nowMs, grazerUsageHalfLifeMs});
    usage.lastUsedAtMs = nowMs;
    usage.lastVisitAtMs = nowMs;

    check(near(calculateStrongestReportedUse(usage, nowMs), 4));

    auto oneTab = makeLastUsedAt(nowMs);
    auto sameUrlInAnotherTab = makeLastUsedAt(nowMs);

    check(near(calculateRowFrecency(0, {oneTab, sameUrlInAnotherTab}, nowMs),
               appReportedUseWeight));
};

auto tVisitCountAlone = test("ReportedUsage/a lifetime visit count alone counts nothing") = []
{
    auto usage = protocol::ContextUsage {};
    usage.visitCount = 300;

    check(calculateStrongestReportedUse(usage, nowMs) == 0);
};

auto tOddStamps = test("ReportedUsage/a stamp from the future doesn't grow, a bad half-life counts nothing") = []
{
    check(near(calculateStrongestReportedUse(makeUseCount({3, nowMs + hourMs, hourMs}), nowMs), 3));
    check(near(calculateStrongestReportedUse(makeLastUsedAt(nowMs + hourMs), nowMs), 1));
    check(calculateStrongestReportedUse(makeUseCount({3, nowMs, 0}), nowMs) == 0);
};

auto tWeighed = test("ReportedUsage/a reported use weighs a quarter of a pick made in Rudder") = []
{
    auto usage = makeUseCount({4, nowMs, grazerUsageHalfLifeMs});

    check(near(calculateRowFrecency(1, {usage}, nowMs), 1 + 4 * appReportedUseWeight));
};

auto tCollected = test("ReportedUsage/usage is filed under the context's key, contexts without it are left out") = []
{
    auto used = makeTab("3", "github");
    used.reportedUsage = makeLastUsedAt(nowMs);
    auto unused = makeTab("4", "news");

    auto byKey = ReportedUsageByKey {};
    collectReportedUsage(byKey, grazerId, {used, unused});

    check(byKey.size() == 1);
    check(byKey.contains(contextKey(grazerId, used)));
};

auto tRankingWithout = test("ReportedUsage/rows with no reported usage rank as Rudder's own frecency ranks them") = []
{
    auto all = std::vector<RudderItem> {
        makeGrazer({makeTab("1", "docs-alpha"), makeTab("2", "docs-beta"), makeTab("3", "docs-gamma")})};
    auto own = makeOwnFrecency({{std::string {grazerId} + "/2", 1.0}});
    auto folded = makeRowFrecencyLookup(own, {}, nowMs);

    check(rankTitles(all, "docs", folded) == rankTitles(all, "docs", own));
    check(rankTitles(all, "docs", folded).front() == "docs-beta");
};

auto tRankingWith = test("ReportedUsage/a tab the app reports as used often ranks above one Rudder saw picked once") = []
{
    auto pickedOnce = makeTab("1", "docs-alpha");
    auto usedOften = makeTab("2", "docs-beta");
    usedOften.reportedUsage = makeUseCount({8, nowMs, grazerUsageHalfLifeMs});
    auto all = std::vector<RudderItem> {makeGrazer({pickedOnce, usedOften})};

    auto own = makeOwnFrecency({{contextKey(grazerId, pickedOnce), 1.0}});
    auto reported = ReportedUsageByKey {};
    collectReportedUsage(reported, grazerId, all.front().contexts);
    auto folded = makeRowFrecencyLookup(own, reported, nowMs);

    check(rankTitles(all, "docs", own).front() == "docs-alpha");
    check(rankTitles(all, "docs", folded).front() == "docs-beta");
};
