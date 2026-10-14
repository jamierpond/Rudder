#include <Ranking.h>

#include <NanoTest/NanoTest.h>

#include <map>

using namespace nano;
using namespace rudder;

namespace
{
RudderItem app(const std::string& id, const std::string& title)
{
    auto item = RudderItem {};
    item.id = id;
    item.appId = id;
    item.title = title;
    return item;
}

std::vector<RudderItem> someApps()
{
    return {app("com.apple.Safari", "Safari"),
            app("com.pond.wim", "Wim"),
            app("com.googlecode.iterm2", "iTerm2"),
            app("com.apple.finder", "Finder")};
}

LastUsedFn lastUsedFrom(std::map<std::string, std::int64_t> stamps)
{
    return [stamps = std::move(stamps)](const std::string& id)
    {
        auto it = stamps.find(id);
        return it != stamps.end() ? it->second : 0;
    };
}

std::vector<std::string> titles(const std::vector<RudderItem>& items)
{
    auto out = std::vector<std::string> {};

    for (auto& item: items)
        out.push_back(item.title);

    return out;
}
} // namespace

auto tRecencyOrders = test("Ranking/most recently used apps come first") = []
{
    auto ordered = orderByRecency(
        someApps(), lastUsedFrom({{"com.pond.wim", 30}, {"com.apple.finder", 20}}));

    check(titles(ordered)
          == std::vector<std::string> {"Wim", "Finder", "Safari", "iTerm2"});
};

auto tEmptyQueryKeepsAll = test("Ranking/an empty query keeps the list as is") = []
{ check(titles(rankItems(someApps(), "")) == titles(someApps())); };

auto tQueryFilters = test("Ranking/a query keeps only matching apps") = []
{
    auto ranked = rankItems(someApps(), "term");

    check(titles(ranked) == std::vector<std::string> {"iTerm2"});
};

auto tBestHitFirst = test("Ranking/a prefix hit outranks a scattered one") = []
{
    auto apps = std::vector<RudderItem> {app("a", "Visual Studio Code"),
                                         app("b", "VS Code")};
    auto ranked = rankItems(apps, "vs");

    check(ranked.front().title == "VS Code");
};

auto tNoMatch = test("Ranking/nothing matching gives an empty list") = []
{ check(rankItems(someApps(), "zzz").empty()); };

auto tContextsFlatten = test("Ranking/contexts match as rows of their own") = []
{
    auto apps = someApps();
    apps[1].contexts = {{"tab-1", "GitHub", "", "https://github.com"},
                        {"tab-2", "Hacker News", "", ""}};

    auto ranked = rankItems(apps, "wim github");

    check(!ranked.empty());

    if (ranked.empty())
        return;

    check(ranked.front().kind == "context");
    check(ranked.front().appId == "com.pond.wim");
    check(ranked.front().contextId == "tab-1");
    check(ranked.front().subtitle == "Wim");
};

auto tContextsHiddenWhenBrowsing = test("Ranking/contexts stay children with no query")
    = []
{
    auto apps = someApps();
    apps[1].contexts = {{"tab-1", "GitHub", "", ""}};

    check(rankItems(apps, "").size() == apps.size());
};

auto tSelectionSkipsFront = test("Ranking/no query starts on the previous app") = []
{
    auto apps = someApps();

    check(initialSelection(apps, "", "com.apple.Safari") == 1);
    check(initialSelection(apps, "", "com.other") == 0);
    check(initialSelection(apps, "wim", "com.apple.Safari") == 0);
    check(initialSelection({apps[0]}, "", "com.apple.Safari") == 0);
};

namespace
{
RudderItem closed(const std::string& id, const std::string& title)
{
    auto item = app(id, title);
    item.running = false;
    item.path = "/Applications/" + title + ".app";
    return item;
}
} // namespace

auto tMergeOnce = test("Ranking/installed apps merge in once, after running ones") = []
{
    auto merged = mergeInstalled(
        someApps(),
        {closed("com.apple.Safari", "Safari"), closed("com.apple.Music", "Music")});

    check(titles(merged)
          == std::vector<std::string> {"Safari", "Wim", "iTerm2", "Finder", "Music"});
    check(merged.front().running);
    check(!merged.back().running);
    check(merged.back().path == "/Applications/Music.app");
};

auto tBrowsingHidesClosed = test("Ranking/no query lists running apps only") = []
{
    auto merged = mergeInstalled(someApps(), {closed("com.apple.Music", "Music")});

    check(titles(rankItems(merged, "")) == titles(someApps()));
};

auto tQueryFindsClosed = test("Ranking/a query finds apps that aren't running") = []
{
    auto merged = mergeInstalled(someApps(), {closed("com.apple.Music", "Music")});
    auto ranked = rankItems(merged, "music");

    check(ranked.size() == 1);
    check(!ranked.front().running);
};

auto tRunningWinsTies = test("Ranking/running beats closed on an equal match") = []
{
    // The closed one comes first in the list, so only the tie-break can lift
    // the running one above it.
    auto apps = std::vector<RudderItem> {closed("a", "Notes"), app("b", "Notes")};
    auto ranked = rankItems(apps, "notes");

    check(ranked.size() == 2);
    check(ranked.front().running);
};

auto tBetterMatchWinsOverRunning = test("Ranking/a better match beats running") = []
{
    // Mid-word in the running app, a word start in the closed one.
    auto apps = std::vector<RudderItem> {app("a", "Affinity"), closed("b", "Finder")};
    auto ranked = rankItems(apps, "fin");

    check(ranked.front().title == "Finder");
};

auto tPreviewNeverLaunches = test("Ranking/selecting a closed app doesn't launch it")
    = []
{
    auto music = closed("com.apple.Music", "Music");

    check(previewActionFor(music, false, "com.apple.Safari") == PreviewAction::none);

    // Even if something claims it is running, a closed row is not previewed.
    check(previewActionFor(music, true, "com.apple.Safari") == PreviewAction::none);
};

auto tPreviewQuitApp = test("Ranking/an app that quit since listing isn't previewed")
    = []
{
    check(previewActionFor(app("com.pond.wim", "Wim"), false, "com.apple.Safari")
          == PreviewAction::none);
};

auto tPreviewRunning = test("Ranking/a running app previews; the origin restores") = []
{
    check(previewActionFor(app("com.pond.wim", "Wim"), true, "com.apple.Safari")
          == PreviewAction::bringForward);
    check(previewActionFor(app("com.apple.Safari", "Safari"), true, "com.apple.Safari")
          == PreviewAction::restoreOrigin);
};

auto tWindowsMatchWithApp = test("Ranking/windows match on app name and title") = []
{
    auto apps = someApps();
    auto window = RudderContext {"w1", "Rudder — README.md", "", ""};
    window.kind = "window";
    apps[2].contexts = {window};

    auto ranked = rankItems(apps, "iterm readme");

    check(!ranked.empty());

    if (ranked.empty())
        return;

    check(ranked.front().kind == "window");
    check(ranked.front().appId == "com.googlecode.iterm2");
    check(ranked.front().contextId == "w1");
};

auto tContextsMatchUrl = test("Ranking/contexts match on their url too") = []
{
    auto apps = someApps();
    apps[1].contexts = {{"tab-1", "Pull requests", "", "https://github.com/pulls"}};

    auto ranked = rankItems(apps, "github pulls");

    check(!ranked.empty() && ranked.front().contextId == "tab-1");
};

auto tUnopenedPlacesNotPreviewed = test("Ranking/a place that isn't open is never previewed")
    = []
{
    auto apps = someApps();
    auto place = RudderContext {"place:/p/tamber-web", "tamber-web", "", ""};
    place.open = false;
    apps[2].contexts = {place};

    auto ranked = rankItems(apps, "tamber");

    check(!ranked.empty());

    if (ranked.empty())
        return;

    check(!ranked.front().running);
    check(previewActionFor(ranked.front(), true, "com.apple.Safari") == PreviewAction::none);
};
