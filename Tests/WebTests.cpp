#include <Favourites.h>
#include <FuzzyMatch.h>
#include <Ranking.h>
#include <Web.h>

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

std::vector<RudderItem> someApps()
{
    return {app("com.apple.Safari", "Safari"),
            app("com.tinyspeck.slackmacgap", "Slack"),
            app("com.apple.TestFlight", "TestFlight"),
            app("com.apple.finder", "Finder")};
}

RudderItem favourite(const std::string& title, const std::string& url)
{
    auto list = FavouriteList {};
    list.favourites.push_back({title, url});
    return favouriteItems(list).front();
}

std::vector<std::string> kinds(const std::vector<RudderItem>& items)
{
    auto out = std::vector<std::string> {};

    for (auto& item: items)
        out.push_back(item.kind);

    return out;
}
} // namespace

auto tAddresses = test("Web/domains, localhost and IPs are addresses") = []
{
    check(looksLikeAddress("github.com"));
    check(looksLikeAddress("github.com/tamber-inc/tamber-web/pulls"));
    check(looksLikeAddress("news.ycombinator.com/item?id=1"));
    check(looksLikeAddress("GitHub.COM"));
    check(looksLikeAddress("localhost"));
    check(looksLikeAddress("localhost:5173"));
    check(looksLikeAddress("localhost:5173/app"));
    check(looksLikeAddress("192.168.1.2"));
    check(looksLikeAddress("192.168.1.2:8080"));
    check(looksLikeAddress("https://github.com"));
    check(looksLikeAddress("file:///tmp"));
    check(looksLikeAddress("  github.com  "));
};

auto tSearches = test("Web/words, c++ and node.js are searches") = []
{
    check(!looksLikeAddress("foo bar"));
    check(!looksLikeAddress("c++"));
    check(!looksLikeAddress("slack"));
    check(!looksLikeAddress("github.com tamber"));
    check(!looksLikeAddress("1.5"));
    check(!looksLikeAddress("300.1.1.1"));
    check(!looksLikeAddress("localhostfoo"));
    check(!looksLikeAddress("what is ://"));
    check(!looksLikeAddress(""));

    // .js isn't a top-level domain: people searching "node.js" want results
    // about Node, and nodejs.org is the first of them.
    check(!looksLikeAddress("node.js"));
};

auto tFileNames = test("Web/a file-extension TLD needs a path, port or subdomain") = []
{
    check(!looksLikeAddress("main.py"));
    check(!looksLikeAddress("README.md"));
    check(looksLikeAddress("bun.sh/docs"));
    check(!looksLikeAddress("docs.rs"));
    check(looksLikeAddress("docs.rs/serde"));
    check(looksLikeAddress("crates.io"));
};

auto tAddressUrl = test("Web/addresses get https, local ones http, typed schemes stay") = []
{
    check(addressUrl("github.com") == "https://github.com");
    check(addressUrl("github.com/tamber-inc/tamber-web/pulls")
          == "https://github.com/tamber-inc/tamber-web/pulls");
    check(addressUrl("http://github.com") == "http://github.com");
    check(addressUrl("localhost:5173") == "http://localhost:5173");
    check(addressUrl("192.168.1.2:8080") == "http://192.168.1.2:8080");
};

auto tSearchUrl = test("Web/the query is url-encoded into the search url") = []
{
    check(searchUrlFor("foo bar", "") == "https://www.google.com/search?q=foo+bar");
    check(searchUrlFor("c++ & you", "") == "https://www.google.com/search?q=c%2B%2B+%26+you");
    check(searchUrlFor("  a/b  ", "https://duckduckgo.com/?q=%s")
          == "https://duckduckgo.com/?q=a%2Fb");
    check(searchUrlFor("x", "https://e.com/%s?q=%s") == "https://e.com/x?q=x");
};

auto tRowTitles = test("Web/the row says what Enter does") = []
{
    check(webRow("github.com", {}).title == "Open github.com");
    check(webRow("github.com", {}).url == "https://github.com");
    check(webRow("foo bar", {}).title == "Search Google for “foo bar”");
    check(webRow("foo", {"", "https://duckduckgo.com/?q=%s"}).title
          == "Search duckduckgo.com for “foo”");
    check(webRow("foo", {}).kind == "web");
};

auto tBrowserOrder =
    test("Web/a link opens with openWith, then browser, then the default") = []
{
    check(browserFor("org.mozilla.firefox", "audio.pond.grazer")
          == "org.mozilla.firefox");
    check(browserFor("", "audio.pond.grazer") == "audio.pond.grazer");
    check(browserFor("", "").empty());
};

auto tWeakThreshold = test("Web/scattered letters are weak, a prefix isn't") = []
{
    check(weakMatch("abc", {}));

    // Real scores from the fuzzy matcher.
    auto score = [](const std::string& query, const std::string& title)
    { return *grazer::fuzzyScore(query, title); };

    check(weakMatch("gh", {score("gh", "TestFlight")}));
    check(weakMatch("sfr", {score("sfr", "Safari")}));
    check(!weakMatch("s", {score("s", "Safari")}));
    check(!weakMatch("sla", {score("sla", "Slack")}));
    check(!weakMatch("weather", {score("weather", "Weather")}));
};

auto tNoResultsSearchFirst = test("Web/no results: the search row is the only row") = []
{
    auto ranked = rankWithWeb(someApps(), "foo bar", {});

    check(ranked.size() == 1);
    check(ranked.front().kind == "web");
    check(initialSelection(ranked, "foo bar", "") == 0);
};

auto tWeakSearchFirst = test("Web/weak results: the search row comes first") = []
{
    auto ranked = rankWithWeb(someApps(), "gh", {});

    check(kinds(ranked) == std::vector<std::string> {"web", "app"});
};

auto tStrongSearchLast = test("Web/a good match stays first, search goes last") = []
{
    auto ranked = rankWithWeb(someApps(), "sla", {});

    check(ranked.front().title == "Slack");
    check(ranked.back().kind == "web");
};

auto tEmptyQueryNoRow = test("Web/no web row without a query") = []
{
    check(rankWithWeb(someApps(), "", {}).size() == someApps().size());
    auto spaces = rankWithWeb(someApps(), "   ", {});
    check(std::none_of(spaces.begin(),
                       spaces.end(),
                       [](const RudderItem& item) { return item.kind == "web"; }));
};

auto tAddressFirst = test("Web/an address opens first even over fuzzy matches") = []
{
    auto apps = someApps();
    apps.push_back(app("com.github.GitHubClient", "GitHub Desktop"));
    auto ranked = rankWithWeb(apps, "github.com", {});

    check(ranked.front().kind == "web");
    check(ranked.front().title == "Open github.com");
};

auto tFavouriteWins = test("Web/an address a favourite has picks the favourite") = []
{
    auto all = someApps();
    all.insert(all.begin(), favourite("GitHub", "https://github.com/"));
    auto ranked = rankWithWeb(all, "github.com", {});

    check(ranked.front().kind == "favourite");
    check(std::none_of(ranked.begin(),
                       ranked.end(),
                       [](const RudderItem& item) { return item.kind == "web"; }));
};

auto tOpenTabWins = test("Web/an address an open tab shows picks the tab") = []
{
    auto grazer = app("audio.pond.grazer", "Grazer");
    grazer.contexts.push_back(
        {"7", "Hacker News", "", "https://news.ycombinator.com/"});
    auto all = someApps();
    all.push_back(grazer);

    auto ranked = rankWithWeb(all, "news.ycombinator.com", {});

    check(ranked.front().kind == "context");
    check(ranked.front().contextId == "7");
};

auto tWebNeverPreviews = test("Web/the web row is never previewed") = []
{
    auto row = webRow("github.com", {});
    check(previewActionFor(row, true, "") == PreviewAction::none);
};

auto tConfigDefaults = test("Web/browser and searchUrl default when missing") = []
{
    auto result = parseFavourites(R"({"favourites": []})");

    check(result.list.has_value());
    check(result.list->browser.empty());
    check(result.list->searchUrl.empty());
    check(webRow("foo", {result.list->browser, result.list->searchUrl}).url
          == "https://www.google.com/search?q=foo");
};

auto tConfigReads = test("Web/browser and searchUrl are read from the file") = []
{
    auto result = parseFavourites(
        R"({"browser": "audio.pond.grazer", "searchUrl": "https://kagi.com/search?q=%s"})");

    check(result.list.has_value());
    check(result.list->browser == "audio.pond.grazer");
    check(result.list->searchUrl == "https://kagi.com/search?q=%s");
};

auto tConfigErrors = test("Web/bad browser or searchUrl values are errors") = []
{
    check(parseFavourites(R"({"browser": 3})").error
          == "rudder.json: \"browser\" must be a string");
    check(parseFavourites(R"({"searchUrl": "https://kagi.com"})").error
          == "rudder.json: \"searchUrl\" needs a %s where the query goes");
};
