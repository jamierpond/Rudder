#include "TestSupport.h"

#include <Frecency.h>
#include <Ranking.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace nano;
using namespace rudder;

// A week of a heavy user's picks, run through FrecencyStore and scoreItems:
// does the order come out the way a person would expect it to? Deterministic
// -- every pick is at a fixed time -- and it prints the table it checks, so
// `just test` shows the numbers (the same columns as `just frecency-report`).

namespace
{
constexpr std::int64_t hourMs = 60LL * 60 * 1000;
constexpr std::int64_t dayMs = 24 * hourMs;

// Monday 00:00 of the simulated week; any fixed stamp would do.
constexpr std::int64_t monday = 1'790'000'000'000LL;

constexpr auto cowTermId = "com.eacp.cowterm";

struct Use
{
    std::string key;
    int perDay = 0;

    // The days (0 = Monday) it is used on.
    std::vector<int> days;
};

std::vector<int> everyDay() { return {0, 1, 2, 3, 4, 5, 6}; }

std::string branchKey(const std::string& branch)
{
    return std::string {cowTermId} + "/branch:" + branch;
}

// Picks spread evenly over a 09:00-18:00 working day.
std::vector<std::pair<std::int64_t, std::string>> schedule(const std::vector<Use>& uses)
{
    auto picks = std::vector<std::pair<std::int64_t, std::string>> {};

    for (auto& use: uses)
        for (auto day: use.days)
            for (int i = 0; i < use.perDay; ++i)
                picks.push_back({monday + day * dayMs + 9 * hourMs
                                     + (std::int64_t) (9 * hourMs * (i + 0.5) / use.perDay),
                                 use.key});

    std::sort(picks.begin(), picks.end());
    return picks;
}

const std::vector<Use>& week()
{
    static const auto uses = std::vector<Use> {
        {"com.tinyspeck.slackmacgap", 40, everyDay()},
        {cowTermId, 30, everyDay()},
        {"company.thebrowser.Browser", 25, everyDay()},
        {"net.whatsapp.WhatsApp", 10, everyDay()},
        {"com.hnc.Discord", 2, everyDay()},
        {"com.apple.Notes", 1, {0, 2, 4, 6}},
        {"com.apple.mail", 1, {0}},
        // A pane worked in all Monday and Tuesday, then left open and idle.
        {branchKey("fix-login"), 25, {0, 1}},
        // A pane used a few times every day.
        {branchKey("rename"), 5, everyDay()},
        // A branch picked as often, but it has no pane open (a place).
        {branchKey("deploy"), 5, everyDay()},
    };
    return uses;
}

// Plays every pick up to `untilMs` into `store`.
void play(FrecencyStore& store, std::int64_t untilMs)
{
    for (auto& [at, key]: schedule(week()))
        if (at <= untilMs)
            store.touch(key, at);
}

// The old curve (k = 2, cap 8, 7-day half-life), for the comparison column.
double oldFrecency(const std::string& key, std::int64_t atMs)
{
    constexpr auto oldHalfLife = 7.0 * dayMs;
    auto total = 0.0;

    for (auto& [at, picked]: schedule(week()))
        if (picked == key && at <= atMs)
            total += std::exp2(-(double) (atMs - at) / oldHalfLife);

    return total;
}

double oldBonus(double frecency)
{
    return frecency <= 0 ? 0 : std::min(8.0, 2.0 * std::log2(1 + frecency));
}

RudderItem app(const std::string& id, const std::string& title)
{
    auto item = RudderItem {};
    item.id = item.appId = id;
    item.title = title;
    return item;
}

RudderContext claudePane(const std::string& id, const std::string& branch, bool open = true)
{
    auto context = RudderContext {id, "claude", "", ""};
    context.branch = branch;
    context.path = "/p/" + branch;
    context.open = open;
    return context;
}

// CowTerm with four panes all titled "claude", so the query "claude"
// matches each exactly as well and only frecency and openness order them.
RudderItem cowTerm()
{
    auto item = app(cowTermId, "CowTerm");
    item.running = true;
    item.contexts = {claudePane("pane:fix", "fix-login"),
                     claudePane("pane:rename", "rename"),
                     claudePane("place:deploy", "deploy", false),
                     claudePane("pane:triage", "triage")};
    return item;
}

std::vector<std::string> claudeOrder(FrecencyStore& store, std::int64_t atMs)
{
    auto order = std::vector<std::string> {};

    for (auto& scored:
         scoreItems({cowTerm()},
                    "claude",
                    [&](const std::string& key) { return store.frecency(key, atMs); }))
        if (!scored.item.contextId.empty())
            order.push_back(scored.item.contextId);

    return order;
}

int rankOf(const std::vector<std::string>& order, const std::string& id)
{
    return (int) (std::find(order.begin(), order.end(), id) - order.begin());
}

void printTable(FrecencyStore& store, std::int64_t atMs, const char* when)
{
    std::printf("\n  frecency after the simulated week, %s\n", when);
    std::printf("  %-42s %8s %6s   %8s %6s\n", "key", "f", "bonus", "old f", "old");

    auto rows = std::vector<std::pair<double, std::string>> {};

    for (auto& use: week())
        rows.push_back({store.frecency(use.key, atMs), use.key});

    std::sort(rows.rbegin(), rows.rend());

    for (auto& [f, key]: rows)
    {
        auto old = oldFrecency(key, atMs);
        std::printf("  %-42s %8.1f %6.2f   %8.1f %6.2f\n",
                    key.c_str(),
                    f,
                    frecencyBonus(f),
                    old,
                    oldBonus(old));
    }

    std::printf("\n");
}
} // namespace

auto tSimulatedWeekOrder
    = test("FrecencySim/a heavy week: busy and recent first, and no two tie at the cap") = []
{
    auto db = testing::freshDatabase("frecency-sim-week");
    auto store = FrecencyStore {db};
    auto sunday = monday + 6 * dayMs + 18 * hourMs;

    play(store, sunday);
    printTable(store, sunday, "Sunday 18:00");

    auto bonus = [&](const std::string& key) { return frecencyBonus(store.frecency(key, sunday)); };

    // Used more, ranked higher -- every step strictly, none at the cap.
    auto apps = std::vector<std::string> {"com.tinyspeck.slackmacgap",
                                          cowTermId,
                                          "company.thebrowser.Browser",
                                          "net.whatsapp.WhatsApp",
                                          "com.hnc.Discord",
                                          "com.apple.Notes",
                                          "com.apple.mail"};

    for (std::size_t i = 0; i + 1 < apps.size(); ++i)
        check(bonus(apps[i]) > bonus(apps[i + 1]));

    check(bonus(apps.front()) < frecencyCap);

    // Under the old curve the four busiest apps all sat at the cap: frecency
    // couldn't tell Slack from WhatsApp.
    for (std::size_t i = 0; i < 4; ++i)
        check(oldBonus(oldFrecency(apps[i], sunday)) == 8.0);
};

auto tAbandonedFallsBelowSteady
    = test("FrecencySim/a pane worked in hard then left falls below a steady one within days")
    = []
{
    auto db = testing::freshDatabase("frecency-sim-abandoned");
    auto store = FrecencyStore {db};
    auto tuesdayEvening = monday + dayMs + 18 * hourMs;

    play(store, tuesdayEvening);

    // While it's the pane of the moment, it leads.
    auto tuesday = claudeOrder(store, tuesdayEvening);
    check(rankOf(tuesday, "pane:fix") < rankOf(tuesday, "pane:rename"));

    // From Wednesday it's left; the steady pane passes it within the week.
    // The rest of the week plays in hour by hour, checking as it goes.
    auto picks = schedule(week());
    auto next = std::find_if(picks.begin(),
                             picks.end(),
                             [&](const auto& pick) { return pick.first > tuesdayEvening; });
    auto passedAt = std::int64_t {0};

    for (auto at = tuesdayEvening; at <= monday + 7 * dayMs && !passedAt; at += hourMs)
    {
        for (; next != picks.end() && next->first <= at; ++next)
            store.touch(next->second, next->first);

        auto order = claudeOrder(store, at);

        if (rankOf(order, "pane:rename") < rankOf(order, "pane:fix"))
            passedAt = at;
    }

    check(passedAt > 0);

    auto daysAfter = (double) (passedAt - tuesdayEvening) / (double) dayMs;
    std::printf("\n  steady pane passes the abandoned one %.1f days after it was left"
                " (old curve: %s)\n\n",
                daysAfter,
                oldFrecency(branchKey("rename"), monday + 7 * dayMs)
                        > oldFrecency(branchKey("fix-login"), monday + 7 * dayMs)
                    ? "within the week"
                    : "not within the week");

    check(daysAfter < 5.0);
};

auto tClosedBelowOpenAtEqualMatch
    = test("FrecencySim/an unopened place ranks below an open pane at an equal match") = []
{
    auto db = testing::freshDatabase("frecency-sim-closed");
    auto store = FrecencyStore {db};
    auto sunday = monday + 6 * dayMs + 18 * hourMs;

    play(store, sunday);

    auto order = claudeOrder(store, sunday);

    // deploy is picked as often as rename but has no pane: rename, open,
    // stays above it -- and so does triage, open and never picked.
    check(rankOf(order, "pane:rename") < rankOf(order, "place:deploy"));
    check(rankOf(order, "pane:triage") < rankOf(order, "place:deploy"));
    check(store.frecency(branchKey("deploy"), sunday) > 0);
};
