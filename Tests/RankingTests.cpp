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

    check(previewActionFor(music, false, "com.apple.Safari") == PreviewAction::none);

    // Even if something claims it is running, a closed row is not previewed.
    check(previewActionFor(music, true, "com.apple.Safari") == PreviewAction::none);
};

auto tPreviewQuitApp =
    test("Ranking/an app that quit since listing isn't previewed") = []
{
    check(previewActionFor(
              app("audio.pond.grazer", "Grazer"), false, "com.apple.Safari")
          == PreviewAction::none);
};

auto tPreviewRunning =
    test("Ranking/a running app previews; the origin restores") = []
{
    check(previewActionFor(
              app("audio.pond.grazer", "Grazer"), true, "com.apple.Safari")
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

namespace
{
// CowTerm as it really pushes: one pane per tab with its cwd (shells say
// nothing else), Claude Code reporting its version as the command, and the
// project places that no pane covers. Captured while Grazer was still called
// Wim; the tests below hinge on the letters w, i, m, so it keeps the old name.
std::vector<RudderItem> cowTermAndWim()
{
    auto cowTerm = app("com.eacp.cowterm", "CowTerm");
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

auto tAppThenProject = test("Ranking/app name then project finds that pane first") = []
{
    for (auto query: {"cowtermwim", "cowterm wim", "cowtwim", "termwim"})
    {
        auto ranked = rankItems(cowTermAndWim(), query);

        check(rankOf(ranked, "pane:5") == 0);
    }
};

auto tProjectAloneFindsPane = test("Ranking/a project name finds its pane near the top")
    = []
{
    auto ranked = rankItems(cowTermAndWim(), "wim");

    // The Wim app itself first, then the pane in ~/projects/Wim -- ahead of
    // the Wim tabs that only match on their app's name.
    check(!ranked.empty() && ranked.front().id == "audio.pond.wim");
    check(rankOf(ranked, "pane:5") == 1);

    // "w" of CowTerm + "im" of GameExperiments still matches, just lower.
    check(rankOf(ranked, "pane:3") > rankOf(ranked, "pane:5"));
};

auto tAppNameAloneListsContextsBelowApp
    = test("Ranking/an app's name lists its contexts below the app") = []
{
    auto ranked = rankItems(cowTermAndWim(), "cowterm");

    check(!ranked.empty() && ranked.front().id == "com.eacp.cowterm");
    check(ranked.size() == 8);
};

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

    auto cowTerm = app("com.eacp.cowterm", "CowTerm");
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

    // The Grazer app is a perfect title match; the pane is right under it,
    // above Grazer's tabs (app name only) and every directory that merely
    // has the letters (graphics-analyzer, grader, razor-blog).
    check(!ranked.empty() && ranked.front().id == "audio.pond.grazer");
    check(rankOf(ranked, "pane:keychain") == 1);
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
    check(countFrom(ranked, "com.eacp.cowterm") == 1);
};

auto tScatteredPathsDontFlood =
    test("Ranking/letters scattered through paths and subtitles don't match") = []
{
    // u(sers) j(amie)p(on)d: in every path, all of it far apart.
    check(countFrom(rankItems(busyCowTerm(), "ujpd"), "com.eacp.cowterm") == 0);

    // Ta(b) (1)2 · mai(n) in the subtitles, likewise.
    check(countFrom(rankItems(busyCowTerm(), "b1n"), "com.eacp.cowterm") == 0);
};

auto tTitleBeatsSubtitle = test("Ranking/a title match beats the same on a subtitle") = []
{
    auto cowTerm = app("com.eacp.cowterm", "CowTerm");
    auto onTitle = RudderContext {"pane:site", "~/projects/main-site", "Tab 1 · pane 1 · dev", ""};
    onTitle.path = "/Users/jamiepond/projects/main-site";
    cowTerm.contexts = {keychainPane(), onTitle};

    auto ranked = rankItems({cowTerm}, "main");

    check(rankOf(ranked, "pane:site") == 0);
    check(rankOf(ranked, "pane:keychain") == 1, "the subtitle's branch still finds it");
};

auto tDirNameBeatsSubtitleAndPath =
    test("Ranking/a directory's name beats its subtitle and full path") = []
{
    auto cowTerm = app("com.eacp.cowterm", "CowTerm");

    auto byDir = RudderContext {"pane:dir", "claude", "", ""};
    byDir.path = "/Users/j/projects/sonic";

    auto bySubtitle = RudderContext {"pane:sub", "claude", "Tab 2 · pane 1 · sonic", ""};

    auto byPath = RudderContext {"pane:path", "claude", "", ""};
    byPath.path = "/Users/j/sonic/lib";

    cowTerm.contexts = {byPath, bySubtitle, byDir};

    auto ranked = rankItems({cowTerm}, "sonic");

    check(rankOf(ranked, "pane:dir") == 0);
    check(rankOf(ranked, "pane:sub") == 1);
    check(rankOf(ranked, "pane:path") == 2);
};

auto tOriginContextsPreview =
    test("Ranking/the origin app's contexts and windows preview; its row restores") = []
{
    auto all = busyCowTerm();
    auto ranked = rankItems(all, "keychain");
    auto& pane = ranked[(std::size_t) rankOf(ranked, "pane:keychain")];

    check(previewActionFor(pane, true, "com.eacp.cowterm") == PreviewAction::bringForward);
    check(previewActionFor(all.front(), true, "com.eacp.cowterm")
          == PreviewAction::restoreOrigin);

    auto window = RudderItem {};
    window.kind = "window";
    window.id = "com.eacp.cowterm/w1";
    window.appId = "com.eacp.cowterm";
    window.contextId = "w1";
    check(previewActionFor(window, true, "com.eacp.cowterm") == PreviewAction::bringForward);
};
