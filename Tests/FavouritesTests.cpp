#include <Favourites.h>
#include <Ranking.h>

#include <NanoTest/NanoTest.h>

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
