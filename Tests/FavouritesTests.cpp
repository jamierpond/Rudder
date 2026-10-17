#include <Favourites.h>
#include <Ranking.h>

#include <NanoTest/NanoTest.h>
#include <emberstore/Emberstore.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace nano;
using namespace rudder;

namespace
{
RudderItem app(const std::string& id, const std::string& title)
{
    auto item = RudderItem {};
    item.id = item.appId = id;
    item.title = title;
    return item;
}

FavouriteList prs()
{
    auto favourite = Favourite {};
    favourite.title = "tamber-web PRs";
    favourite.url = "https://github.com/tamber-inc/tamber-web/pulls";
    favourite.keywords = {"prs", "pulls"};
    return {{favourite}};
}
} // namespace

auto tParses = test("Favourites/a well-formed file parses") = []
{
    auto result = parseFavourites(R"({
        // a comment, and a URL with // in it
        "favourites": [
            {"title": "tamber-web PRs",
             "url": "https://github.com/tamber-inc/tamber-web/pulls",
             "keywords": ["prs", "pulls"]},
            {"title": "Grazer", "app": "audio.pond.grazer"},
            /* block comment */
            {"title": "Projects", "path": "~/projects", "openWith": "com.apple.finder"}
        ]
    })");

    check(result.error.empty());
    check(result.list.has_value() && result.list->favourites.size() == 3);

    if (!result.list || result.list->favourites.size() != 3)
        return;

    check(result.list->favourites[0].url == "https://github.com/tamber-inc/tamber-web/pulls");
    check(result.list->favourites[0].keywords.size() == 2);
    check(result.list->favourites[1].app == "audio.pond.grazer");
};

auto tEmptyDefaults = test("Favourites/an empty document is no favourites") = []
{
    auto result = parseFavourites("{}");

    check(result.error.empty());
    check(result.list.has_value() && result.list->favourites.empty());
};

auto tBadInput = test("Favourites/bad edits are errors, not empty favourites") = []
{
    auto failsWith = [](std::string_view text)
    {
        auto result = parseFavourites(text);
        return !result.list.has_value() && !result.error.empty();
    };

    check(failsWith("{ not json"));
    check(failsWith("[]"));
    check(failsWith(R"({"favourites": {}})"));
    check(failsWith(R"({"favourites": [42]})"));
    check(failsWith(R"({"favourites": [{"url": "https://x.com"}]})"));
    check(failsWith(R"({"favourites": [{"title": "Nothing"}]})"));
    check(failsWith(R"({"favourites": [{"title": "Two", "url": "u", "app": "a"}]})"));
    check(failsWith(R"({"favourites": [{"title": 7, "url": "u"}]})"));
    check(failsWith(R"({"favourites": [{"title": "K", "url": "u", "keywords": "prs"}]})"));
};

auto tErrorsNameTheEntry = test("Favourites/an error names the favourite") = []
{
    auto result = parseFavourites(R"({"favourites": [{"title": "Broken"}]})");

    check(result.error.find("Broken") != std::string::npos);
};

namespace
{
const auto laptop = Frame {0.f, 33.f, 1512.f, 949.f};

const auto withPanel = R"({
    "browser": "audio.pond.grazer",
    "favourites": [
        {"title": "PRs", "url": "https://github.com/pulls", "keywords": ["prs", "pulls"]}
    ],
    "panel": {
        "0,33 1512x949": {"x": 850.5, "y": 84, "width": 640, "height": 420}
    }
})";
} // namespace

auto tPanelParses = test("Favourites/the panel frames parse") = []
{
    auto result = parseFavourites(withPanel);

    check(result.error.empty());
    check(result.list && result.list->panel);

    if (!result.list)
        return;

    check(savedFrame(result.list->panel, laptop) == Frame {850.5f, 84.f, 640.f, 420.f});
    check(result.list->favourites.size() == 1);
};

auto tNoPanel = test("Favourites/no panel is every screen centred") = []
{
    auto result = parseFavourites(R"({"favourites": []})");

    check(result.list && !result.list->panel);

    if (result.list)
        check(!savedFrame(result.list->panel, laptop));
};

auto tBadPanel = test("Favourites/a bad panel frame is an error naming it") = []
{
    auto failsWith = [](std::string_view text, std::string_view named)
    {
        auto result = parseFavourites(text);
        return !result.list && result.error.find(named) != std::string::npos;
    };

    check(failsWith(R"({"panel": []})", "\"panel\""));
    check(failsWith(R"({"panel": {"a": 3}})", "panel \"a\""));
    check(failsWith(R"({"panel": {"a": {"x": 0, "y": 0, "width": "640", "height": 420}}})",
                    "\"width\""));
    check(failsWith(R"({"panel": {"a": {"x": 0, "y": 0, "width": 640}}})", "\"height\""));
    check(failsWith(R"({"panel": {"a": {"x": 0, "y": 0, "width": 0, "height": 420}}})",
                    "above 0"));
};

auto tSaveKeepsTheRest = test("Favourites/saving a frame keeps the rest of the file") = []
{
    auto dir = std::filesystem::temp_directory_path() / "RudderTests" / "config-save";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto path = dir / "rudder.json";

    auto before = parseFavourites(withPanel);
    check(before.list.has_value());

    if (!before.list)
        return;

    auto config = *before.list;
    auto external = Frame {1512.f, 0.f, 2560.f, 1415.f};
    saveFrame(config.panel, external, {1700.f, 300.f, 900.f, 600.f});

    auto document = emberstore::Document<FavouriteList> {eacp::FilePath {path.string()}};
    check(document.set(config));

    auto file = std::ifstream {path};
    auto after = parseFavourites(std::string {std::istreambuf_iterator<char> {file}, {}});

    check(after.error.empty());

    if (!after.list)
        return;

    check(after.list->favourites == before.list->favourites);
    check(after.list->favourites[0].keywords == std::vector<std::string> {"prs", "pulls"});
    check(after.list->browser == "audio.pond.grazer");
    check(savedFrame(after.list->panel, laptop) == savedFrame(before.list->panel, laptop));
    check(savedFrame(after.list->panel, external) == Frame {1700.f, 300.f, 900.f, 600.f});

    // Every frame reset: no "panel" left in the file.
    forgetFrame(config.panel, laptop);
    forgetFrame(config.panel, external);
    check(document.set(config));

    auto reset = std::ifstream {path};
    auto text = std::string {std::istreambuf_iterator<char> {reset}, {}};
    check(text.find("panel") == std::string::npos);
    check(text.find("PRs") != std::string::npos);
};

auto tMigration = test("Favourites/the old panel-frames.json is taken in once") = []
{
    auto old = R"({"0,33 1512x949": {"height": 420, "width": 640, "x": 850.5, "y": 84}})";
    auto error = std::string {};

    auto fresh = *parseFavourites(R"({"browser": "b"})").list;
    check(adoptPanelFrames(fresh, old, error));
    check(error.empty());
    check(savedFrame(fresh.panel, laptop) == Frame {850.5f, 84.f, 640.f, 420.f});
    check(fresh.browser == "b");

    // A panel of its own already wins.
    auto own = *parseFavourites(withPanel).list;
    auto ownFrame = savedFrame(own.panel, laptop);
    auto other = R"({"0,33 1512x949": {"x": 1, "y": 1, "width": 500, "height": 400}})";
    check(!adoptPanelFrames(own, other, error));
    check(savedFrame(own.panel, laptop) == ownFrame);

    // A broken old file changes nothing and says why.
    auto broken = *parseFavourites("{}").list;
    check(!adoptPanelFrames(broken, "{ nope", error));
    check(!error.empty() && !broken.panel);
};

auto tBrowsingByRecency = test("Favourites/no query: favourites take their place by recency") = []
{
    auto all = std::vector<RudderItem> {app("com.apple.Safari", "Safari"),
                                        app("audio.pond.grazer", "Grazer")};
    auto starred = favouriteItems(prs());
    all.insert(all.end(), starred.begin(), starred.end());

    // Never used: below the running apps.
    auto ranked = rankItems(orderByRecency(all, [](const std::string&) { return 0; }), "");

    check(ranked.size() == 3);
    check(ranked.back().kind == "favourite");
    check(initialSelection(ranked, "", "com.apple.Safari") == 1);
    check(initialSelection(ranked, "", "com.other") == 0);

    // Used more lately than Grazer: above it.
    auto key = frecencyKey(starred.front());
    auto used = orderByRecency(all,
                               [&](const std::string& k) -> std::int64_t
                               {
                                   return k == "com.apple.Safari" ? 30
                                          : k == key              ? 20
                                          : k == "audio.pond.grazer" ? 10
                                                                     : 0;
                               });
    ranked = rankItems(used, "");

    check(ranked[0].title == "Safari");
    check(ranked[1].kind == "favourite");
    check(ranked[2].title == "Grazer");
    check(initialSelection(ranked, "", "com.apple.Safari") == 1);
};

auto tFavouriteDoesntHideApp = test("Favourites/a favourite of an app doesn't hide the closed app") = []
{
    auto favourite = Favourite {};
    favourite.title = "Grazer";
    favourite.app = "audio.pond.grazer";
    auto rows = favouriteItems({{favourite}});

    auto merged = mergeInstalled(rows, {app("audio.pond.grazer", "Grazer")});

    check(merged.size() == 2);
    check(!merged.back().running);
};

auto tKeywordsMatch = test("Favourites/keywords find a favourite") = []
{
    auto ranked = rankItems(favouriteItems(prs()), "pulls");

    check(ranked.size() == 1);
};

auto tFavouritesRankByMatch
    = test("Favourites/a favourite ranks by match and frecency, not a flat boost") = []
{
    auto all = std::vector<RudderItem> {app("com.github.desktop", "PRs")};
    auto favourites = favouriteItems(prs());
    all.insert(all.end(), favourites.begin(), favourites.end());

    // The app's name is the closer match...
    auto ranked = rankItems(all, "prs");
    check(ranked.size() == 2 && ranked.front().kind == "app");

    // ...but not by much, so a favourite picked a few times comes first.
    auto key = frecencyKey(favourites.front());
    ranked = rankItems(all, "prs", [&](const std::string& k) { return k == key ? 3.0 : 0.0; });
    check(ranked.size() == 2 && ranked.front().kind == "favourite");
};

auto tNormalise = test("Favourites/urls compare without fragment or trailing slash")
    = []
{
    check(normaliseUrl("https://GitHub.com/tamber-inc/Pulls/#top")
          == "https://github.com/tamber-inc/Pulls");
    check(normaliseUrl("https://x.com/") == normaliseUrl("https://x.com"));
};

auto tReusesOpenTab = test("Favourites/an open tab with the url is found") = []
{
    auto grazer = app("audio.pond.grazer", "Grazer");
    grazer.contexts = {{"7",
                        "Pull requests",
                        "",
                        "https://github.com/tamber-inc/tamber-web/pulls/"}};

    auto found =
        contextForUrl({grazer}, "https://github.com/tamber-inc/tamber-web/pulls#x");

    check(found.has_value());

    if (found)
    {
        check(found->appId == "audio.pond.grazer");
        check(found->contextId == "7");
    }

    check(!contextForUrl({grazer}, "https://github.com/other").has_value());
};

auto tToggle = test("Favourites/toggling adds once and removes by target") = []
{
    auto list = FavouriteList {};
    auto tab = RudderItem {};
    tab.kind = "context";
    tab.title = "PRs";
    tab.url = "https://github.com/pulls";
    tab.appId = "audio.pond.grazer";

    auto favourite = favouriteFor(tab);

    check(favourite.has_value() && favourite->url == "https://github.com/pulls");
    check(toggleFavourite(list, *favourite));
    check(list.favourites.size() == 1);

    // Its own row toggles it off again.
    auto row = favouriteItems(list).front();
    check(!toggleFavourite(list, *favouriteFor(row)));
    check(list.favourites.empty());
};

auto tAppRowsBecomeApps = test("Favourites/an app or window row favourites its app") = []
{
    auto window = RudderItem {};
    window.kind = "window";
    window.title = "README.md";
    window.subtitle = "Xcode";
    window.appId = "com.apple.dt.Xcode";

    auto favourite = favouriteFor(window);

    check(favourite.has_value() && favourite->app == "com.apple.dt.Xcode");
    check(favourite.has_value() && favourite->title == "Xcode");
    check(!favouriteFor(app("pid:42", "Helper")).has_value());
};
