#include <Ranking.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
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
            app("audio.pond.grazer", "Grazer"),
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
        someApps(),
        lastUsedFrom({{"audio.pond.grazer", 30}, {"com.apple.finder", 20}}));

    check(titles(ordered)
          == std::vector<std::string> {"Grazer", "Finder", "Safari", "iTerm2"});
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

    auto ranked = rankItems(apps, "grazer github");

    check(!ranked.empty());

    if (ranked.empty())
        return;

    check(ranked.front().kind == "context");
    check(ranked.front().appId == "audio.pond.grazer");
    check(ranked.front().contextId == "tab-1");
    check(ranked.front().subtitle == "Grazer");
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
    check(initialSelection(apps, "grazer", "com.apple.Safari") == 0);
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
          == std::vector<std::string> {
              "Safari", "Grazer", "iTerm2", "Finder", "Music"});
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

    check(!isPreviewable(music, false));

    // Even if something claims it is running, a closed row is not previewed.
    check(!isPreviewable(music, true));
};

auto tPreviewQuitApp =
    test("Ranking/an app that quit since listing isn't previewed") = []
{
    check(!isPreviewable(app("audio.pond.grazer", "Grazer"), false));
};

auto tPreviewRunning =
    test("Ranking/a running app previews, the origin app too") = []
{
    check(isPreviewable(app("audio.pond.grazer", "Grazer"), true));
    check(isPreviewable(app("com.apple.Safari", "Safari"), true));
};

auto tWindowsMatchWithApp = test("Ranking/windows match on app name and title") = []
{
    auto apps = someApps();
    auto window = RudderContext {"w1", "Rudder — README.md", "", ""};
    window.kind = "window";
    auto otherWindow = RudderContext {"w2", "Grazer — main.cpp", "", ""};
    otherWindow.kind = "window";
    apps[2].contexts = {window, otherWindow};

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
    check(!isPreviewable(ranked.front(), true));
};

namespace
{
// CowTerm as it really pushes: one pane per tab with its cwd (shells say
// nothing else), Claude Code reporting its version as the command, and the
// project places that no pane covers. Captured while Grazer was still called
// Wim; the tests below hinge on the letters w, i, m, so it keeps the old name.
std::vector<RudderItem> cowTermAndWim()
{
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");
    cowTerm.contexts = {
        {"pane:1", "~/projects/old-stuff/tamber-web-analytics-macos-shortcut", "", ""},
        {"pane:2", "~/projects/old-stuff/tamber-web-sonic-atlas-breakout-simplify-ci-more", "", ""},
        {"pane:3", "~/projects/GameExperiments", "", ""},
        {"pane:4", "2.1.299 · Fix window matching — ~/projects/Rudder", "", ""},
        {"pane:5", "2.1.299 · Rudder fuzzy match bug — ~/projects/Wim", "", ""},
        {"pane:6", "~/projects/CowTerm", "", ""}};

    auto place = RudderContext {"place:/Users/j/projects/swift2mm", "swift2mm", "", ""};
    place.open = false;
    cowTerm.contexts.push_back(place);

    auto wimApp = app("audio.pond.wim", "Wim");

    for (auto i = 0; i < 11; ++i)
        wimApp.contexts.push_back({"tab-" + std::to_string(i),
                                   "Tab " + std::to_string(i),
                                   "",
                                   "https://example.com/" + std::to_string(i)});

    return {wimApp, cowTerm, app("com.googlecode.iterm2", "iTerm2")};
}

int rankOf(const std::vector<RudderItem>& ranked, const std::string& contextId)
{
    for (std::size_t i = 0; i < ranked.size(); ++i)
        if (ranked[i].contextId == contextId)
            return (int) i;

    return -1;
}
} // namespace

namespace
{
// The pane Jamie couldn't find: Claude Code set its title to the
// conversation, so the project is only in its path (and CowTerm's subtitle
// names the tab and its branch).
RudderContext keychainPane()
{
    auto pane = RudderContext {"pane:keychain",
                               "2.1.299 · Password autofill and apple keychain",
                               "Tab 3 · pane 1 · main",
                               ""};
    pane.path = "/Users/jamiepond/projects/Grazer";
    return pane;
}

// CowTerm at its real size: the pane above among ~140 zsh panes (title the
// cwd, as a shell at its prompt says nothing else) and places not open yet,
// with Grazer and a few of its tabs running alongside.
std::vector<RudderItem> busyCowTerm()
{
    const auto names = std::vector<std::string> {
        "tamber-web", "tamber-api", "CowTerm", "Rudder", "eacp", "emberstore",
        "Miro", "NanoTest", "iac", "dotfiles", "swift2mm", "GameExperiments",
        "graphics-analyzer", "grader", "gaze-tracker", "garden-planner", "rage-clicks",
        "eacp-updater", "acme-suite", "sonic-atlas", "keys-and-chords", "web-audio-lab",
        "old-stuff", "zsh-config", "razor-blog", "laser-cutter", "eraser-tool",
        "tamber-docs", "tamber-infra", "tamber-ios", "tamber-android", "pond-site",
        "wave-editor", "midi-router", "synth-voice", "dsp-notes", "shader-toy",
        "audio-graph", "grid-layout", "terraform-modules", "k8s-manifests",
        "ci-scripts", "release-notes", "design-system", "icons", "fonts",
        "data-pipeline", "analytics", "llm-evals", "prompt-lab", "agents",
        "notebook", "scratch", "playground", "benchmarks", "perf-traces",
        "crash-reports", "bug-bash", "feature-flags", "auth-service",
        "billing-service", "search-service", "sync-engine", "file-watcher",
        "image-cache", "video-encoder", "pdf-renderer", "markdown-parser",
        "json-schema", "proto-defs"};

    auto cowTerm = app("audio.pond.cowterm", "CowTerm");
    auto tab = 0;

    for (auto& name: names)
    {
        auto pane = RudderContext {"pane:" + name,
                                   "~/projects/" + name,
                                   "Tab " + std::to_string(++tab) + " · pane 1 · " + name,
                                   ""};
        pane.path = "/Users/jamiepond/projects/" + name;
        cowTerm.contexts.push_back(pane);

        auto second = RudderContext {"pane:" + name + "/lib",
                                     "~/projects/" + name + "/lib",
                                     "Tab " + std::to_string(tab) + " · pane 2 · main",
                                     ""};
        second.path = "/Users/jamiepond/projects/" + name + "/lib";
        cowTerm.contexts.push_back(second);
    }

    cowTerm.contexts.insert(cowTerm.contexts.begin() + 40, keychainPane());

    for (auto name: {"Unbrowse", "skills", "web-scraper", "voice-memos", "ear-trainer"})
    {
        auto place = RudderContext {"place:/Users/jamiepond/projects/" + std::string {name},
                                    name,
                                    "~/projects/" + std::string {name},
                                    ""};
        place.path = "/Users/jamiepond/projects/" + std::string {name};
        place.open = false;
        cowTerm.contexts.push_back(place);
    }

    auto grazerApp = app("audio.pond.grazer", "Grazer");

    for (auto i = 0; i < 5; ++i)
        grazerApp.contexts.push_back({"tab-" + std::to_string(i),
                                      "Tab " + std::to_string(i),
                                      "",
                                      "https://example.com/" + std::to_string(i)});

    return {cowTerm, grazerApp, app("com.apple.Safari", "Safari")};
}

int countFrom(const std::vector<RudderItem>& ranked, const std::string& appId)
{
    return (int) std::count_if(ranked.begin(),
                               ranked.end(),
                               [&](const RudderItem& item)
                               { return item.appId == appId && item.kind != "app"; });
}
} // namespace

auto tProjectFindsRetitledPane =
    test("Ranking/a project name finds a pane whose title says something else") = []
{
    auto all = busyCowTerm();
    check(all.front().contexts.size() > 140);

    auto ranked = rankItems(all, "grazer");

    // The pane comes first, above Grazer's tabs (app name only) and every
    // directory that merely has the letters (graphics-analyzer, grader,
    // razor-blog); the Grazer app row is shadowed by its tabs.
    check(rankOf(ranked, "pane:keychain") == 0);
    check(rankOf(ranked, "pane:graphics-analyzer") > rankOf(ranked, "tab-0"));
};

auto tAppThenProjectByPath =
    test("Ranking/app name then project finds the pane by its directory") = []
{
    for (auto query: {"cowtermgrazer", "cowterm grazer"})
        check(rankOf(rankItems(busyCowTerm(), query), "pane:keychain") == 0);
};

auto tTitleStillFinds = test("Ranking/the title still finds the pane") = []
{
    auto ranked = rankItems(busyCowTerm(), "keychain");

    check(rankOf(ranked, "pane:keychain") == 0);

    // Nothing else has those letters close together; the subtitles and long
    // paths add no stragglers.
    check(countFrom(ranked, "audio.pond.cowterm") == 1);
};

auto tScatteredPathsDontFlood =
    test("Ranking/letters scattered through paths and subtitles don't match") = []
{
    // u(sers) j(amie)p(on)d: in every path, all of it far apart.
    check(countFrom(rankItems(busyCowTerm(), "ujpd"), "audio.pond.cowterm") == 0);

    // Ta(b) (1)2 · mai(n) in the subtitles, likewise.
    check(countFrom(rankItems(busyCowTerm(), "b1n"), "audio.pond.cowterm") == 0);
};

auto tBranchNotShown = test("Ranking/the provider names the row; Rudder doesn't add the branch") = []
{
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");

    auto pane = RudderContext {"pane:1", "main · claude", "Tab 1", ""};
    pane.branch = "main";
    cowTerm.contexts = {pane};

    auto ranked = rankItems({cowTerm}, "claude");

    check(ranked[(std::size_t) rankOf(ranked, "pane:1")].title == "main · claude");
    check(ranked[(std::size_t) rankOf(ranked, "pane:1")].subtitle == "CowTerm · Tab 1");
};

auto tOriginContextsPreview =
    test("Ranking/the origin app, its contexts and its windows preview") = []
{
    auto all = busyCowTerm();
    auto ranked = rankItems(all, "keychain");
    auto& pane = ranked[(std::size_t) rankOf(ranked, "pane:keychain")];

    check(isPreviewable(pane, true));
    check(isPreviewable(all.front(), true));

    auto window = RudderItem {};
    window.kind = "window";
    window.id = "audio.pond.cowterm/w1";
    window.appId = "audio.pond.cowterm";
    window.contextId = "w1";
    check(isPreviewable(window, true));
};

auto tFavouriteKeyedByTarget = test("Ranking/a favourite's frecency is keyed by what it opens")
    = []
{
    auto favourite = app("fav:3", "GitHub");
    favourite.kind = "favourite";
    favourite.url = "https://github.com/";

    check(frecencyKey(favourite) == "fav:https://github.com/");
    check(frecencyKey(app("com.apple.Safari", "Safari")) == "com.apple.Safari");
};

auto tClosedBelowOpen = test("Ranking/a branch with no pane ranks below an open pane that matches as well") = []
{
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");

    auto place = RudderContext {"place:fix", "fix-login", "", ""};
    place.open = false;
    auto pane = RudderContext {"pane:fix", "fix-logout", "Tab 1", ""};
    cowTerm.contexts = {place, pane};

    auto ranked = rankItems({cowTerm}, "fixlog");

    check(rankOf(ranked, "pane:fix") < rankOf(ranked, "place:fix"));
};

// --- Stable keys, recent contexts, renames -----------------------------------

namespace
{
RudderContext pane(const std::string& id,
                   const std::string& title,
                   const std::string& branch,
                   const std::string& path,
                   const std::string& repo = "/p")
{
    auto context = RudderContext {id, title, "", ""};
    context.branch = branch;
    context.repo = repo;
    context.path = path;
    return context;
}

RudderItem rowFor(const std::vector<RudderItem>& ranked, const std::string& contextId)
{
    for (auto& item: ranked)
        if (item.contextId == contextId)
            return item;

    return {};
}
} // namespace

auto tContextKeyStable
    = test("Ranking/a context is keyed by repository and branch, else path, else id") = []
{
    auto cowTerm = "audio.pond.cowterm";

    check(contextKey(cowTerm, pane("pane:aaa", "x", "fix-login", "/p/web"))
          == "audio.pond.cowterm/repo:/p#branch:fix-login");
    check(contextKey(cowTerm, pane("pane:bbb", "x", "", "/p/notes"))
          == "audio.pond.cowterm/path:/p/notes");
    check(contextKey(cowTerm, pane("tab-4", "x", "", "")) == "audio.pond.cowterm/tab-4");
};

auto tSameBranchInOtherReposDiffers
    = test("Ranking/the same branch in two repositories is two contexts") = []
{
    auto cowTerm = "audio.pond.cowterm";
    auto inEacp = pane("pane:1", "x", "develop", "/p/eacp", "/p/eacp");
    auto inWeb = pane("pane:2", "x", "develop", "/p/tamber-web", "/p/tamber-web");

    check(contextKey(cowTerm, inEacp) == "audio.pond.cowterm/repo:/p/eacp#branch:develop");
    check(contextKey(cowTerm, inEacp) != contextKey(cowTerm, inWeb));
};

auto tBranchWithoutRepoKeysByPath
    = test("Ranking/a branch without a repository is keyed by its path") = []
{
    check(contextKey("audio.pond.cowterm", pane("pane:1", "x", "develop", "/p/web", ""))
          == "audio.pond.cowterm/path:/p/web");
};

auto tPaneAndPlaceShareKey
    = test("Ranking/a new shell and its branch's place share the row's key") = []
{
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");
    auto place = pane("place:/p/web-fix", "web-fix", "fix-login", "/p/web-fix", "/p/web-fix");
    place.open = false;
    cowTerm.contexts = {pane("pane:111", "claude", "fix-login", "/p/web-fix/src", "/p/web-fix"),
                        pane("pane:222", "claude", "fix-login", "/p/web-fix", "/p/web-fix"),
                        place};

    auto key = std::string {"audio.pond.cowterm/repo:/p/web-fix#branch:fix-login"};

    for (auto query: {"claude", "web-fix"})
    {
        auto ranked = rankItems({cowTerm}, query);

        check(ranked.size() == 1);
        check(!ranked.empty() && frecencyKey(ranked.front()) == key);
    }
};

namespace
{
constexpr std::int64_t hour = 60LL * 60 * 1000;
constexpr std::int64_t now = 1000 * hour;

Recency recencyOf(std::map<std::string, std::int64_t> stamps, const std::string& front)
{
    auto lastUsed = lastUsedFrom(std::move(stamps));
    return {[lastUsed, front](const std::string& key)
            { return key == front ? INT64_MAX : lastUsed(key); },
            now,
            front};
}

std::vector<RudderItem> workday()
{
    auto safari = app("com.apple.Safari", "Safari");
    auto grazer = app("audio.pond.grazer", "Grazer");
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");
    auto focused = pane("pane:3", "claude", "rudder-rename", "/p/rudder");
    focused.focused = true;
    cowTerm.contexts = {pane("pane:1", "claude", "fix-login", "/p/web"),
                        pane("pane:2", "vim", "old-work", "/p/old"),
                        focused};
    return {safari, grazer, cowTerm};
}
} // namespace

auto tEmptyListHasRecentContexts
    = test("Ranking/no query lists recent contexts among the apps, by recency") = []
{
    auto stamps = std::map<std::string, std::int64_t> {
        {"audio.pond.grazer", now - 3 * hour},
        {"audio.pond.cowterm", now - 1 * hour},
        {"audio.pond.cowterm/repo:/p#branch:fix-login", now - 2 * hour},
        {"audio.pond.cowterm/repo:/p#branch:old-work", now - 72 * hour},
        {"audio.pond.cowterm/repo:/p#branch:rudder-rename", now - 1 * hour}};

    auto list = rankItems(workday(), "", {}, recencyOf(stamps, "com.apple.Safari"));

    // Safari is in front; CowTerm's focused pane came with its activation;
    // CowTerm itself isn't listed again beside its panes; three-day-old work
    // isn't listed.
    check(titles(list) == std::vector<std::string> {"Safari", "claude", "claude", "Grazer"});
    check(list[1].contextId == "pane:3");
    check(list[2].contextId == "pane:1");
    check(initialSelection(list, "", "com.apple.Safari") == 1);
};

auto tEmptyListSkipsWhereYouAre
    = test("Ranking/no query leaves out the front app's focused context") = []
{
    auto stamps = std::map<std::string, std::int64_t> {
        {"audio.pond.grazer", now - 3 * hour},
        {"audio.pond.cowterm/repo:/p#branch:fix-login", now - 2 * hour},
        {"audio.pond.cowterm/repo:/p#branch:rudder-rename", now - 1 * hour}};

    auto list = rankItems(workday(), "", {}, recencyOf(stamps, "audio.pond.cowterm"));

    // CowTerm's "Current" row is shadowed by its other pane, so the
    // selection starts on the first row.
    check(titles(list) == std::vector<std::string> {"claude", "Grazer", "Safari"});
    check(list[0].contextId == "pane:1");
    check(initialSelection(list, "", "audio.pond.cowterm") == 0);
};

auto tEmptyListKeepsAppWithoutRecentContexts
    = test("Ranking/no query keeps an app's row when none of its contexts is listed") = []
{
    auto stamps = std::map<std::string, std::int64_t> {
        {"audio.pond.grazer", now - 3 * hour},
        {"audio.pond.cowterm", now - 1 * hour},
        {"audio.pond.cowterm/repo:/p#branch:old-work", now - 72 * hour}};

    auto list = rankItems(workday(), "", {}, recencyOf(stamps, "com.apple.Safari"));

    check(titles(list) == std::vector<std::string> {"Safari", "CowTerm", "Grazer"});
    check(initialSelection(list, "", "com.apple.Safari") == 1);
};

auto tTypedQueryKeepsAppWithoutContexts
    = test("Ranking/a typed query keeps the row of an app with no contexts") = []
{
    auto ranked = rankItems(workday(), "grazer");

    check(!ranked.empty() && ranked.front().id == "audio.pond.grazer");
};

auto tEmptyListLimited = test("Ranking/no query lists at most a handful of contexts, one per key")
    = []
{
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");
    auto stamps = std::map<std::string, std::int64_t> {};

    for (int i = 0; i < 10; ++i)
    {
        auto branch = "b" + std::to_string(i);
        cowTerm.contexts.push_back(pane("pane:" + branch, branch, branch, "/p"));
        stamps["audio.pond.cowterm/repo:/p#branch:" + branch] = now - i * hour;
    }

    // A second pane and a place on b0: still one row, the open pane.
    auto place = pane("place:/p/b0", "b0 place", "b0", "/p/b0");
    place.open = false;
    cowTerm.contexts.insert(cowTerm.contexts.begin(), place);
    cowTerm.contexts.push_back(pane("pane:b0-again", "b0 again", "b0", "/p"));

    auto list = rankItems({cowTerm}, "", {}, recencyOf(stamps, "other"));
    auto contexts = std::count_if(list.begin(),
                                  list.end(),
                                  [](const RudderItem& item) { return item.kind == "context"; });

    check(contexts == (long) recentContextLimit);
    check(list.front().contextId == "pane:b0");
    check(list.front().running);
};

auto tRenamedRowShowsName = test("Ranking/a renamed context shows its name, the old title after")
    = []
{
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");
    auto named = pane("pane:1", "claude", "fix-login", "/p");
    named.subtitle = "Tab 1";
    named.name = "Login bug";
    cowTerm.contexts = {named};

    auto row = rowFor(rankItems({cowTerm}, "login"), "pane:1");

    check(row.title == "Login bug");
    check(row.subtitle == "claude · CowTerm · Tab 1");
};

// --- Gone contexts ---------------------------------------------------------

auto tClosedTabIsGone = test("Ranking/a tab missing from the next push is gone") = []
{
    auto grazer = "audio.pond.grazer";
    auto before = std::vector<RudderContext> {{"3", "GitHub", "", "https://github.com"},
                                              {"4", "Gmail", "", "https://mail.google.com"}};
    auto after = std::vector<RudderContext> {{"4", "Gmail", "", "https://mail.google.com"}};

    check(goneContextKeys(grazer, before, after)
          == std::vector<std::string> {"audio.pond.grazer/3"});
    check(goneContextKeys(grazer, before, before).empty());
    check(goneContextKeys(grazer, {}, after).empty());
};

auto tKeyStillCarriedIsntGone
    = test("Ranking/a closed pane whose branch another pane or place carries isn't gone") = []
{
    auto cowTerm = "audio.pond.cowterm";
    auto place = pane("place:/p/web-fix", "web-fix", "fix-login", "/p/web-fix");
    place.open = false;

    auto before = std::vector<RudderContext> {pane("pane:1", "claude", "fix-login", "/p/web-fix"),
                                              pane("pane:2", "zsh", "fix-login", "/p/web-fix"),
                                              place,
                                              pane("pane:3", "zsh", "notes", "/p/notes")};

    // pane:1 closes; pane:2 is still on fix-login.
    check(goneContextKeys(cowTerm, before, {before[1], before[2], before[3]}).empty());

    // Both panes close; the place keeps the key.
    check(goneContextKeys(cowTerm, before, {before[2], before[3]}).empty());

    // Everything on fix-login goes: one key, once.
    check(goneContextKeys(cowTerm, before, {before[3]})
          == std::vector<std::string> {"audio.pond.cowterm/repo:/p#branch:fix-login"});
};

auto tMovedPaneIsntGone
    = test("Ranking/a pane that changed branch or directory isn't gone") = []
{
    auto cowTerm = "audio.pond.cowterm";
    auto before = std::vector<RudderContext> {pane("pane:1", "zsh", "fix-login", "/p/web"),
                                              pane("pane:2", "zsh", "", "/p/notes")};
    auto after = std::vector<RudderContext> {pane("pane:1", "zsh", "main", "/p/web"),
                                             pane("pane:2", "zsh", "", "/p/other")};

    check(goneContextKeys(cowTerm, before, after).empty());
};

auto tIdOnlyKeys = test("Ranking/only contexts with no branch or path are keyed by id alone") = []
{
    auto grazer = "audio.pond.grazer";
    auto contexts = std::vector<RudderContext> {{"3", "GitHub", "", "https://github.com"},
                                                pane("pane:1", "zsh", "fix-login", ""),
                                                pane("pane:2", "zsh", "", "/p/notes"),
                                                {"3", "GitHub again", "", ""}};

    check(idOnlyContextKeys(grazer, contexts)
          == std::vector<std::string> {"audio.pond.grazer/3"});
};

// --- What a pick counts, and the curve at heavy volume -------------------------

auto tPickWhereYouAre
    = test("Ranking/Enter on the app summoned over, or the pane it shows, isn't a pick") = []
{
    auto cowTerm = app("audio.pond.cowterm", "CowTerm");
    auto shown = pane("pane:1", "claude", "fix-login", "/p/web");
    shown.focused = true;
    auto other = pane("pane:2", "zsh", "rename", "/p/rudder");
    cowTerm.contexts = {shown, other};

    auto shownRow = rowFor(rankItems({cowTerm}, "claude"), "pane:1");
    auto otherRow = rowFor(rankItems({cowTerm}, "zsh"), "pane:2");

    check(!pickMoves(cowTerm, "audio.pond.cowterm", cowTerm.contexts));
    check(!pickMoves(shownRow, "audio.pond.cowterm", cowTerm.contexts));
    check(pickMoves(otherRow, "audio.pond.cowterm", cowTerm.contexts));
    check(pickMoves(app("com.apple.Safari", "Safari"), "audio.pond.cowterm", cowTerm.contexts));

    // Summoned over another app, every CowTerm row goes somewhere.
    check(pickMoves(cowTerm, "com.apple.Safari", {}));
    check(pickMoves(shownRow, "com.apple.Safari", {}));
};

