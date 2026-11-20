#include <AppOrders.h>
#include <CowTermPalette/RankingMaterials.h>
#include <GrazerPalette/RankingMaterials.h>
#include <Providers.h>
#include <Ranking.h>

#include <FrecencySearch/LearnedPicks.h>
#include <FrecencySearch/MatchLevel.h>
#include <Miro/Reflection/Serialize.h>
#include <NanoTest/NanoTest.h>

#include <map>
#include <string>
#include <vector>

using namespace nano;
using namespace rudder;

// Rudder lists an app's rows in the app's own order once the app hands it its
// ranking materials: these tests send the materials through the protocol, the
// way the app does, and hold Rudder's order of that app's rows to the order
// the app's own palette gives the same inputs, whatever Rudder knows besides.

namespace
{
constexpr std::int64_t nowMs = 1'790'000'000'000LL;
constexpr std::int64_t minuteMs = 60LL * 1000;
constexpr std::int64_t hourMs = 60 * minuteMs;
constexpr auto grazerId = "audio.pond.grazer";
constexpr auto cowTermId = "audio.pond.cowterm";
constexpr auto otherAppId = "com.example.notes";

// Tied tabs, a failed tab, the current tab, a place not open, a used page with
// no tab and a past search.
grazer::PaletteRankingMaterials makeGrazerMaterials()
{
    auto materials = grazer::PaletteRankingMaterials {};
    materials.tabs = {
        {.tabId = 1, .title = "Pull requests", .url = "https://github.com/pulls", .lastViewedAtMs = nowMs - hourMs},
        {.tabId = 2, .title = "Issues", .url = "https://github.com/issues", .lastViewedAtMs = nowMs - hourMs},
        {.tabId = 3, .title = "github.com/broken", .url = "https://github.com/broken", .failed = true},
        {.tabId = 4, .title = "GitHub", .url = "https://github.com/", .lastViewedAtMs = nowMs, .isCurrent = true}};
    materials.places = {{"GitHub Actions", "https://github.com/actions"},
                        {"Hacker News", "https://news.ycombinator.com/"}};
    materials.stampsByNormalizedUrl = {{"github.com/pulls", {nowMs - hourMs, 3}},
                                       {"github.com/issues", {nowMs - hourMs, 3}},
                                       {"github.com/notifications", {nowMs - 2 * hourMs, 5}},
                                       {"news.ycombinator.com", {nowMs - 48 * hourMs, 2}}};
    materials.history = {
        {"https://github.com/notifications", "Notifications", nowMs - 2 * hourMs, 9},
        {"https://github.com/settings", "Settings", nowMs - 72 * hourMs, 1},
        {"https://www.google.com/search?q=github", "github - Google Search", nowMs - hourMs, 1}};
    return materials;
}

// An unread Claude session, two sessions used at one moment, projects, a
// remote project, an unreachable host and a pick learned for "gr".
term::PaletteRankingMaterials makeCowTermMaterials()
{
    using Kind = term::PaletteItem::Kind;
    auto materials = term::PaletteRankingMaterials {};
    auto unread = term::UnreadMark {"finished", nowMs - minuteMs, "Done."};
    materials.items = {
        {.kind = Kind::Session, .key = "/p/Rudder", .label = "Rudder", .detail = "/p/Rudder", .claude = true,
         .lastUsed = nowMs - 5 * minuteMs, .unread = unread},
        {.kind = Kind::Session, .key = "/p/Grazer", .label = "Grazer", .detail = "/p/Grazer",
         .lastUsed = nowMs - 30 * minuteMs},
        {.kind = Kind::Session, .key = "/p/grader", .label = "grader", .detail = "/p/grader",
         .lastUsed = nowMs - 30 * minuteMs},
        {.kind = Kind::Project, .key = "/p/Grazer-wt", .label = "Grazer worktree", .detail = "/p/Grazer-wt"},
        {.kind = Kind::Project, .key = "macprorack:/home/j/miro", .label = "miro", .detail = "/home/j/miro",
         .host = "jamie@macprorack"},
        {.kind = Kind::Project, .label = "tamby — unreachable", .detail = "no route", .host = "tamby",
         .unavailable = true}};
    materials.usageByKey = {{"/p/Rudder", {4, nowMs - hourMs}},
                            {"/p/Grazer", {2, nowMs - hourMs}},
                            {"/p/grader", {2, nowMs - hourMs}}};
    materials.learnedPicks = {{"gr", "/p/Grazer-wt", 3, nowMs - 10 * minuteMs}};
    return materials;
}

// What the app does: encode its materials into a protocol line. What Rudder
// does: decode the line and read the materials.
template <typename Materials>
AppOrdering sendThroughProtocol(const Materials& materials, const char* ordering)
{
    auto line = protocol::encode(protocol::RankingMaterials {ordering, Miro::toJSON(materials)});
    auto message = protocol::decode(line);
    auto* reply = message ? std::get_if<protocol::RankingMaterials>(&*message) : nullptr;
    auto read = reply ? readAppOrdering(*reply) : std::nullopt;
    return read.value_or(AppOrdering {});
}

// Grazer's palette: listPaletteRows and rankPalette with each page's
// MruStore usage decayed to now, less the search and open-URL rows, named the
// way Grazer's activate takes them.
std::vector<std::string> orderAsGrazersPaletteDoes(const grazer::PaletteRankingMaterials& materials,
                                                   const OrderingRequest& request)
{
    auto usage = [&materials](const std::string& normalizedUrl)
    {
        auto found = materials.stampsByNormalizedUrl.find(normalizedUrl);

        if (found == materials.stampsByNormalizedUrl.end())
            return 0.0;

        return frecencysearch::calculateDecayedUsage(found->second.usage, found->second.at, nowMs);
    };

    auto rows = grazer::listPaletteRows(materials.tabs, materials.places, usage);
    auto ids = std::vector<std::string> {};

    for (auto& row: grazer::rankPalette(rows, materials.history, {request.query, grazer::PaletteMode::goFromHere, usage}))
    {
        if (row.kind == GoKind::search || row.kind == GoKind::typedURL)
            continue;

        ids.push_back(row.tabId >= 0 ? std::to_string(row.tabId) : "url:" + row.url);
    }

    return ids;
}

// CowTerm's palette: orderPaletteItems with PaletteUsageStore's decayed counts
// and learned picks, named the way CowTerm's activate takes them.
std::vector<std::string> orderAsCowTermsPaletteDoes(const term::PaletteRankingMaterials& materials,
                                                    const OrderingRequest& request)
{
    auto usage = [&materials](const std::string& key)
    {
        auto found = materials.usageByKey.find(key);

        if (found == materials.usageByKey.end())
            return 0.0;

        return frecencysearch::calculateDecayedUsage(found->second.count, found->second.at, nowMs);
    };

    auto picks = std::vector<frecencysearch::LearnedPick> {};

    for (auto& entry: materials.learnedPicks)
        picks.push_back({entry.query, entry.destinationKey, entry.count, entry.at});

    auto learned =
        frecencysearch::findLearnedPicksForQuery(picks, frecencysearch::normaliseQuery(request.query), nowMs);
    auto ids = std::vector<std::string> {};

    for (auto& row: term::orderPaletteItems(materials.items, request.query, {usage, learned}))
        ids.push_back((term::isSessionItem(row) ? "session:" : "place:") + row.key);

    return ids;
}

RudderItem makeApp(const std::string& id, const std::string& title)
{
    auto app = RudderItem {};
    app.id = app.appId = id;
    app.title = title;
    return app;
}

// The panel's rows: Grazer with a pushed tab per material tab, CowTerm with a
// pushed pane, and an app with no materials whose window titles match too.
std::vector<RudderItem> makeAllRows()
{
    auto grazer = makeApp(grazerId, "Grazer");
    grazer.contexts = {{"1", "Pull requests", "", "https://github.com/pulls"},
                       {"2", "Issues", "", "https://github.com/issues"},
                       {"3", "github.com/broken", "", "https://github.com/broken"},
                       {"4", "GitHub", "", "https://github.com/"}};
    auto cowTerm = makeApp(cowTermId, "CowTerm");
    cowTerm.contexts = {{"pane:1", "zsh", "", ""}};
    auto notes = makeApp(otherAppId, "Notes");
    notes.contexts = {{"n1", "github notes", "", ""}, {"n2", "grazer notes", "", ""}};
    return {grazer, cowTerm, notes};
}

std::map<std::string, std::vector<std::string>> listContextIdsByApp(const std::vector<RudderItem>& rows)
{
    auto idsByApp = std::map<std::string, std::vector<std::string>> {};

    for (auto& row: rows)
        if (row.kind == "context")
            idsByApp[row.appId].push_back(row.contextId);

    return idsByApp;
}

// Rudder's own usage, built to disagree with the apps: the last of each app's
// rows is the one Rudder saw picked most, and a pick learned for "git" names
// Grazer's settings page.
SearchUsage makeDisagreeingUsage(const OrderingRequest& request, const AppOrderedContexts& appOrders)
{
    auto usage = [](const std::string& key)
    {
        constexpr auto pickedOften = 50.0;
        auto isFavouredByRudder = key.ends_with("/url:https://github.com/settings")
                                  || key.ends_with("/place:macprorack:/home/j/miro")
                                  || key.ends_with("/n2");
        return isFavouredByRudder ? pickedOften : 0.0;
    };

    auto learned = std::vector<frecencysearch::LearnedPick> {
        {"git", std::string {grazerId} + "/url:https://github.com/settings", 3, nowMs}};
    if (request.query != "git")
        learned.clear();

    return {usage, learned, appOrders};
}

const std::vector<std::string> typedTexts {
    "", "g", "gr", "git", "github", "grazer", "issues", "pull", "news", "notif", "set", "broken",
    "rudder", "claude", "miro", "macprorackmiro", "wt", "tamby", "https://github.com/pulls",
    "github.com/issues", "zzz"};

ContextProviders makeProvidersWithMaterials()
{
    auto providers = ContextProviders {};
    providers.connect({"Grazer", grazerId, 1}, [](auto&, bool) {});
    providers.connect({"CowTerm", cowTermId, 2}, [](auto&, bool) {});
    providers.setAppOrdering(
        {grazerId, sendThroughProtocol(makeGrazerMaterials(), grazer::paletteRankingMaterialsName)});
    providers.setAppOrdering(
        {cowTermId, sendThroughProtocol(makeCowTermMaterials(), term::paletteRankingMaterialsName)});
    return providers;
}

std::vector<std::string> listIds(const std::vector<RudderContext>& contexts)
{
    auto ids = std::vector<std::string> {};

    for (auto& context: contexts)
        ids.push_back(context.id);

    return ids;
}
} // namespace

auto tGrazerOrderThroughProtocol = test("AppOrders/Grazer's rows come in Grazer's palette order through the protocol") = []
{
    auto providers = makeProvidersWithMaterials();

    for (auto& typed: typedTexts)
        check(listIds(providers.orderContextsByApp({typed, nowMs})[grazerId])
                  == orderAsGrazersPaletteDoes(makeGrazerMaterials(), {typed, nowMs}),
              "typed: " + typed);
};

auto tCowTermOrderThroughProtocol = test("AppOrders/CowTerm's rows come in CowTerm's palette order through the protocol") = []
{
    auto providers = makeProvidersWithMaterials();

    for (auto& typed: typedTexts)
        check(listIds(providers.orderContextsByApp({typed, nowMs})[cowTermId])
                  == orderAsCowTermsPaletteDoes(makeCowTermMaterials(), {typed, nowMs}),
              "typed: " + typed);
};

auto tPanelKeepsEachAppsOrder = test("AppOrders/the panel lists each app's rows in the app's order, whatever Rudder's own usage says") = []
{
    auto providers = makeProvidersWithMaterials();
    auto all = makeAllRows();

    for (auto& typed: typedTexts)
    {
        if (typed.empty())
            continue;

        auto request = OrderingRequest {typed, nowMs};
        auto appOrders = providers.orderContextsByApp(request);
        auto listed = listContextIdsByApp(rankItems(all, typed, makeDisagreeingUsage(request, appOrders)));

        check(listed[grazerId] == orderAsGrazersPaletteDoes(makeGrazerMaterials(), request),
              "Grazer, typed: " + typed);
        check(listed[cowTermId] == orderAsCowTermsPaletteDoes(makeCowTermMaterials(), request),
              "CowTerm, typed: " + typed);
    }
};

auto tAppWithoutMaterials = test("AppOrders/an app that hands Rudder no materials keeps Rudder's own order") = []
{
    auto providers = makeProvidersWithMaterials();
    auto all = makeAllRows();
    auto request = OrderingRequest {"g", nowMs};
    auto withOrders = listContextIdsByApp(
        rankItems(all, "g", makeDisagreeingUsage(request, providers.orderContextsByApp(request))));
    auto withoutOrders = listContextIdsByApp(rankItems(all, "g", makeDisagreeingUsage(request, {})));

    check(withOrders[otherAppId] == withoutOrders[otherAppId]);
    check(withoutOrders[otherAppId].front() == "n2");
};

auto tPanelWithoutAnyMaterials = test("AppOrders/with no materials at all the panel is Rudder's order as before") = []
{
    auto all = makeAllRows();

    for (auto& typed: typedTexts)
    {
        auto usage = makeDisagreeingUsage({typed, nowMs}, {});
        auto listed = rankItems(all, typed, usage);
        auto asBefore = rankItems(all, typed, {usage.usage, usage.learnedPicks});
        auto listedIds = std::vector<std::string> {};
        auto beforeIds = std::vector<std::string> {};

        for (auto& row: listed)
            listedIds.push_back(row.id);

        for (auto& row: asBefore)
            beforeIds.push_back(row.id);

        check(listedIds == beforeIds, "typed: " + typed);
    }
};

auto tBrowseKeepsGrazersOrder = test("AppOrders/with nothing typed Grazer's recent tabs come in Grazer's order") = []
{
    auto providers = makeProvidersWithMaterials();
    auto all = makeAllRows();
    auto stamps = std::map<std::string, std::int64_t> {{std::string {grazerId} + "/2", nowMs - minuteMs},
                                                       {std::string {grazerId} + "/1", nowMs - 2 * minuteMs},
                                                       {std::string {grazerId} + "/4", nowMs - 3 * minuteMs}};
    auto recency = Recency {[&stamps](const std::string& key)
                            {
                                auto found = stamps.find(key);
                                return found == stamps.end() ? std::int64_t {0} : found->second;
                            },
                            nowMs,
                            otherAppId,
                            providers.orderContextsByApp({{}, nowMs})};

    auto rows = rankItems(all, "", {}, recency);

    check(listContextIdsByApp(rows)[grazerId] == std::vector<std::string> {"1", "2", "4"});
};

auto tUnknownOrdering = test("AppOrders/materials for an ordering Rudder doesn't know are not read") = []
{
    check(!readAppOrdering({"someone-elses-palette", Miro::JSON {Miro::Json::Object {}}}));
};
