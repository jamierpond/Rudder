#include <Ranking.h>

#include <FrecencySearch/LearnedPicks.h>
#include <NanoTest/NanoTest.h>

#include <map>

using namespace nano;
using namespace rudder;

namespace
{
constexpr std::int64_t nowMs = 1'790'000'000'000LL;
constexpr std::int64_t minuteMs = 60LL * 1000;

constexpr auto grazerId = "audio.pond.grazer";
constexpr auto cowTermId = "com.eacp.cowterm";
constexpr auto grazerPaneKey = "com.eacp.cowterm/repo:/Users/j/projects/Grazer#branch:main";

RudderItem makeApp(const std::string& id, const std::string& title)
{
    auto item = RudderItem {};
    item.id = item.appId = id;
    item.title = title;
    return item;
}

RudderContext makeGrazerPane(const std::string& id, const std::string& title)
{
    auto pane = RudderContext {id, title, "Tab 1 · main", ""};
    pane.path = "/Users/j/projects/Grazer";
    pane.repo = pane.path;
    pane.branch = "main";
    return pane;
}

RudderItem makeGrazer()
{
    auto grazer = makeApp(grazerId, "Grazer");
    grazer.contexts = {{"tab-home", "Home", "", "grazer://home"},
                       {"tab-github", "Pull requests", "", "https://github.com/pulls"}};
    return grazer;
}

RudderItem makeCowTerm()
{
    auto cowTerm = makeApp(cowTermId, "CowTerm");
    cowTerm.contexts = {makeGrazerPane("pane:1", "claude"),
                        makeGrazerPane("pane:2", "zsh"),
                        makeGrazerPane("pane:3", "zsh")};
    return cowTerm;
}

std::vector<RudderItem> makeWorkday() { return {makeGrazer(), makeCowTerm()}; }

FrecencyFn makeUsage(std::map<std::string, double> usageByKey)
{
    return [usageByKey = std::move(usageByKey)](const std::string& key)
    {
        auto found = usageByKey.find(key);

        if (found == usageByKey.end())
            return 0.0;

        return found->second;
    };
}

struct RepeatedPick
{
    std::string query;
    std::string destinationKey;
    int times = 1;
    std::int64_t firstPickMs = 0;
};

std::vector<frecencysearch::LearnedPick> pickRepeatedly(std::vector<frecencysearch::LearnedPick> learned,
                                                        const RepeatedPick& repeated)
{
    auto& query = repeated.query;
    auto& destinationKey = repeated.destinationKey;

    for (int pickIndex = 0; pickIndex < repeated.times; ++pickIndex)
    {
        auto pickMs = repeated.firstPickMs + pickIndex * minuteMs;
        auto existing = std::find_if(learned.begin(),
                                     learned.end(),
                                     [&](const frecencysearch::LearnedPick& pick)
                                     { return pick.query == query && pick.destinationKey == destinationKey; });

        if (existing == learned.end())
        {
            learned.push_back(frecencysearch::addPickTo({query, destinationKey}, pickMs));
            continue;
        }

        *existing = frecencysearch::addPickTo(*existing, pickMs);
    }

    return learned;
}

SearchUsage makeSearchUsage(const std::vector<frecencysearch::LearnedPick>& learned,
                            const std::string& query,
                            FrecencyFn usage = {})
{
    return {std::move(usage), frecencysearch::findLearnedPicksForQuery(learned, query, nowMs)};
}

std::vector<std::string> getKeys(const std::vector<RudderItem>& ranked)
{
    auto keys = std::vector<std::string> {};

    for (auto& item: ranked)
        keys.push_back(frecencyKey(item));

    return keys;
}

bool hasContext(const std::vector<RudderItem>& ranked, const std::string& contextId)
{
    return std::any_of(ranked.begin(),
                       ranked.end(),
                       [&](const RudderItem& item) { return item.contextId == contextId; });
}
} // namespace

auto tLearnedPaneFirst = test("SearchOrder/grazer picked twice for the pane lists the pane first") = []
{
    auto learned = pickRepeatedly({}, {"grazer", grazerPaneKey, 2, nowMs - 10 * minuteMs});
    auto ranked = rankItems(makeWorkday(), "grazer", makeSearchUsage(learned, "grazer"));

    check(getKeys(ranked) == std::vector<std::string> {grazerPaneKey, grazerId});
    check(!hasContext(ranked, "tab-home"), "grazer:// is a scheme, not a match");
};

auto tUnlearnedTieByUsage = test("SearchOrder/grazer with nothing learned: app and pane by usage") = []
{
    auto appUsedMore = makeUsage({{grazerId, 5.0}, {grazerPaneKey, 1.0}});
    auto paneUsedMore = makeUsage({{grazerId, 1.0}, {grazerPaneKey, 5.0}});

    check(getKeys(rankItems(makeWorkday(), "grazer", {appUsedMore}))
          == std::vector<std::string> {grazerId, grazerPaneKey});
    check(getKeys(rankItems(makeWorkday(), "grazer", {paneUsedMore}))
          == std::vector<std::string> {grazerPaneKey, grazerId});
};

auto tLongestLearnedPrefix = test("SearchOrder/gr uses its own picks, else the longest learned prefix") = []
{
    auto learned = pickRepeatedly({}, {"g", grazerPaneKey, 2, nowMs - 10 * minuteMs});

    check(rankItems(makeWorkday(), "gr", makeSearchUsage(learned, "gr")).front().appId == cowTermId);

    learned = pickRepeatedly(learned, {"gr", grazerId, 2, nowMs - 5 * minuteMs});

    check(rankItems(makeWorkday(), "gr", makeSearchUsage(learned, "gr")).front().id == grazerId);

    auto paneUsedMore = makeUsage({{grazerPaneKey, 5.0}});
    check(rankItems(makeWorkday(), "gr", {paneUsedMore}).front().appId == cowTermId);
};

auto tAppNameThenWord = test("SearchOrder/grazer github lists the GitHub tab first") = []
{
    auto ranked = rankItems(makeWorkday(), "grazer github");

    check(!ranked.empty() && ranked.front().contextId == "tab-github");
    check(!hasContext(ranked, "tab-home"));
};

auto tSameBranchPanesOneRow = test("SearchOrder/three panes on one branch are one row") = []
{
    auto ranked = rankItems({makeCowTerm()}, "zsh");

    check(ranked.size() == 1);
};

auto tSameBranchRowOpensFocusedPane = test("SearchOrder/a branch's row opens its focused pane") = []
{
    auto cowTerm = makeCowTerm();
    cowTerm.contexts[2].focused = true;

    auto ranked = rankItems({cowTerm}, "grazer");

    check(ranked.size() == 1);
    check(hasContext(ranked, "pane:3"));
};

auto tAppNamedWindowIsAppRow = test("SearchOrder/one window titled as its app leaves the app row") = []
{
    auto grazer = makeApp(grazerId, "Grazer");
    auto window = RudderContext {"w1", "Grazer", "", ""};
    window.kind = "window";
    grazer.contexts = {window};

    auto ranked = rankItems({grazer}, "grazer");

    check(ranked.size() == 1);
    check(!ranked.empty() && ranked.front().kind == "app");
};

auto tScatteredBelowUsage = test("SearchOrder/scattered letters rank below any closer match, whatever the usage") = []
{
    auto apps = std::vector<RudderItem> {makeApp(grazerId, "Grazer"), makeApp("gzr", "Gzr Tool")};
    auto grazerUsedHeavily = makeUsage({{grazerId, 1000.0}});

    check(getKeys(rankItems(apps, "gzr", {grazerUsedHeavily}))
          == std::vector<std::string> {"gzr", grazerId});
};

auto tRepeatedPickHolds = test("SearchOrder/a destination picked three times stays first after one other pick") = []
{
    auto apps = std::vector<RudderItem> {makeApp("cow.y", "Cow Yard"), makeApp("cow.x", "Cow Express")};
    auto learned = pickRepeatedly({}, {"cow", "cow.x", 3, nowMs - 10 * minuteMs});
    learned = pickRepeatedly(learned, {"cow", "cow.y", 1, nowMs - minuteMs});

    check(rankItems(apps, "cow", makeSearchUsage(learned, "cow")).front().id == "cow.x");
};
