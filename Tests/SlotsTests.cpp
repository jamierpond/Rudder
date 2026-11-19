#include <Favourites.h>
#include <Ranking.h>
#include <Slots.h>

#include <NanoTest/NanoTest.h>
#include <emberstore/Emberstore.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace nano;
using namespace rudder;

namespace
{
constexpr auto cowTerm = "audio.pond.cowterm";
constexpr auto grazer = "audio.pond.grazer";
constexpr auto rudderRepo = "/Users/me/projects/Rudder";
constexpr auto keyCodeY = std::uint16_t {0x10};
constexpr auto keyCodeU = std::uint16_t {0x20};

RudderContext
    makePane(const std::string& id, const std::string& branch, bool open = true)
{
    auto context = RudderContext {};
    context.id = id;
    context.title = "zsh";
    context.repo = rudderRepo;
    context.branch = branch;
    context.path = rudderRepo;
    context.open = open;
    return context;
}

RudderContext makeTab(const std::string& id, const std::string& url)
{
    auto context = RudderContext {};
    context.id = id;
    context.title = "Tab " + id;
    context.url = url;
    return context;
}

RudderItem makeRunningApp(const std::string& id,
                          const std::string& title,
                          std::vector<RudderContext> contexts = {})
{
    auto item = RudderItem {};
    item.id = item.appId = id;
    item.title = title;
    item.contexts = std::move(contexts);
    return item;
}

Slot makePaneSlot(const std::string& branch)
{
    auto slot = Slot {};
    slot.title = "Rudder " + branch;
    slot.app = cowTerm;
    slot.repo = rudderRepo;
    slot.branch = branch;
    return slot;
}

Slot makeUrlSlot(const std::string& url)
{
    auto slot = Slot {};
    slot.title = url;
    slot.url = url;
    return slot;
}

bool failsWith(const std::string& text, const std::string& part)
{
    auto result = parseFavourites(text);
    return !result.list && result.error.find(part) != std::string::npos;
}

std::string readFile(const std::filesystem::path& path)
{
    auto file = std::ifstream {path};
    return {std::istreambuf_iterator<char> {file}, {}};
}
} // namespace

auto tSlotsParse =
    test("Slots/a slots list parses, an empty object an empty slot") = []
{
    auto result = parseFavourites(R"({
        "slots": [
            {"title": "Rudder main", "app": "audio.pond.cowterm",
             "repo": "/Users/me/projects/Rudder", "branch": "main"},
            {},
            {"url": "https://github.com"},
            {"app": "com.apple.Notes"},
            {"path": "~/projects"}
        ]
    })");

    check(result.error.empty());

    if (!result.list)
        return;

    auto& slots = result.list->slots;
    check(slots.size() == slotCount);
    check(slots[0] == makePaneSlot("main"));
    check(isSlotEmpty(slots[1]));
    check(slots[2].url == "https://github.com");
    check(slots[3].app == "com.apple.Notes");
    check(slots[4].path == "~/projects");
};

auto tNoSlots = test("Slots/a file without slots has every slot empty") = []
{
    auto result = parseFavourites("{}");

    check(result.list.has_value() && result.list->slots.empty());
};

auto tBadSlots = test("Slots/a bad slot is an error naming the slot and field") = []
{
    check(failsWith(R"({"slots": {}})", R"("slots" must be a list)"));
    check(failsWith(R"({"slots": [{}, {}, {}, {}, {}, {}]})", "at most 5"));
    check(failsWith(R"({"slots": [{}, 3]})", "slot 2 must be an object"));
    check(failsWith(R"({"slots": [{"app": 7}]})",
                    R"(slot 1: "app" must be a string)"));
    check(
        failsWith(R"({"slots": [{"title": "x"}]})", "slot 1 needs exactly one of"));
    check(failsWith(R"({"slots": [{"url": "https://a.com", "app": "b"}]})",
                    "slot 1 needs exactly one of"));
    check(failsWith(R"({"slots": [{"path": "/p", "directory": "/p"}]})",
                    R"(slot 1: "repo", "branch" and "directory" need an "app")"));
    check(failsWith(R"({"slots": [{"app": "a", "branch": "main"}]})",
                    R"(slot 1 needs "repo" with "branch", or "directory")"));
};

auto tSlotsRoundTrip = test("Slots/slots Rudder writes read back the same") = []
{
    auto dir = std::filesystem::temp_directory_path() / "RudderTests" / "slots-save";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto path = dir / "rudder.json";

    auto config = FavouriteList {};
    assignSlot(config.slots, 0, makePaneSlot("main"));
    assignSlot(config.slots, 3, makeUrlSlot("https://github.com"));

    auto document =
        emberstore::Document<FavouriteList> {eacp::FilePath {path.string()}};
    check(document.set(config));

    auto after = parseFavourites(readFile(path));

    check(after.error.empty());
    check(after.list.has_value() && after.list->slots == config.slots);
};

auto tPaneSlot = test("Slots/a pane is slotted by its repository and branch") = []
{
    auto slot = makeSlotForContext(cowTerm, "CowTerm", makePane("pane:1", "main"));

    check(slot.app == cowTerm && slot.repo == rudderRepo && slot.branch == "main");
    check(slot.directory == rudderRepo && slot.url.empty());

    auto renamed = makePane("pane:1", "main");
    renamed.name = "the app";
    check(makeSlotForContext(cowTerm, "CowTerm", renamed).title == "the app");
};

auto tUnnamedContexts = test(
    "Slots/a tab is slotted by its url, a context with only an id by its app") = []
{
    auto tab =
        makeSlotForContext(grazer, "Grazer", makeTab("3", "https://github.com"));
    check(tab.url == "https://github.com" && tab.app.empty());

    auto bare = RudderContext {};
    bare.id = "tab-4";
    auto app = makeSlotForContext(cowTerm, "CowTerm", bare);
    check(app == makeSlotForApp(cowTerm, "CowTerm"));
};

auto tRowSlots = test("Slots/rows become slots, the web row and pid apps never") = []
{
    auto notes = makeRunningApp("com.apple.Notes", "Notes");
    check(makeSlotForRow(notes) == makeSlotForApp("com.apple.Notes", "Notes"));

    auto web = RudderItem {};
    web.kind = "web";
    web.url = "https://google.com/search?q=x";
    check(!makeSlotForRow(web));
    check(!makeSlotForRow(makeRunningApp("pid:42", "Mystery")));

    auto favourite = RudderItem {};
    favourite.kind = "favourite";
    favourite.title = "Projects";
    favourite.path = "~/projects";
    check(makeSlotForRow(favourite)->path == "~/projects");
};

auto tAssignMoves =
    test("Slots/putting a place already in a slot moves it there") = []
{
    auto slots = std::vector<Slot> {};
    assignSlot(slots, 0, makePaneSlot("main"));
    assignSlot(slots, 1, makeUrlSlot("https://github.com/"));

    check(slots.size() == slotCount);

    // Another pane on the same branch, and the same url without its slash.
    assignSlot(slots, 2, makePaneSlot("main"));
    assignSlot(slots, 4, makeUrlSlot("https://github.com"));

    check(isSlotEmpty(slots[0]) && isSlotEmpty(slots[1]));
    check(slots[2] == makePaneSlot("main"));
    check(slots[4].url == "https://github.com");

    assignSlot(slots, 2, makePaneSlot("fix-login"));
    check(slots[2].branch == "fix-login");

    assignSlot(slots, slotCount, makePaneSlot("main"));
    check(slots.size() == slotCount);
};

auto tRowBadge = test("Slots/a row finds the slot holding its place") = []
{
    auto slots = std::vector<Slot> {};
    assignSlot(slots, 1, makePaneSlot("main"));
    assignSlot(slots, 3, makeSlotForApp(cowTerm, "CowTerm"));

    auto app = makeRunningApp(cowTerm, "CowTerm");
    auto otherPane = makeContextRow(app, makePane("pane:9", "main"));
    check(findSlotForRow(slots, otherPane) == std::optional<std::size_t> {1});
    check(findSlotForRow(slots, app) == std::optional<std::size_t> {3});

    // A pane with no stable name isn't the app's slot, nor is a window.
    auto bare = RudderContext {};
    bare.id = "tab-4";
    check(!findSlotForRow(slots, makeContextRow(app, bare)));

    auto window = RudderContext {};
    window.id = "w1";
    window.kind = "window";
    check(!findSlotForRow(slots, makeContextRow(app, window)));
};

auto tOpensLivePane =
    test("Slots/a pane slot opens the open pane on its branch") = []
{
    auto apps = std::vector<RudderItem> {makeRunningApp(
        cowTerm,
        "CowTerm",
        {makePane("pane:old", "main", false), makePane("pane:new", "main")})};

    auto target = resolveSlotTarget(makePaneSlot("main"), apps, {});

    check(target && target->kind == "context" && target->contextId == "pane:new");
    check(target && target->running);
};

auto tOpensPlace =
    test("Slots/a pane slot whose pane is closed opens its unopened place") = []
{
    auto apps = std::vector<RudderItem> {
        makeRunningApp(cowTerm, "CowTerm", {makePane("place:main", "main", false)})};

    auto target = resolveSlotTarget(makePaneSlot("main"), apps, {});

    check(target && target->contextId == "place:main" && !target->running);
};

auto tFallsBackToApp =
    test("Slots/a pane slot with no pane or place opens its app") = []
{
    auto apps = std::vector<RudderItem> {makeRunningApp(cowTerm, "CowTerm")};
    auto running = resolveSlotTarget(makePaneSlot("main"), apps, {});

    check(running && running->kind == "app" && running->appId == cowTerm
          && running->running);

    auto installed =
        std::map<std::string, std::string> {{cowTerm, "/Applications/CowTerm.app"}};
    auto launched = resolveSlotTarget(makePaneSlot("main"), {}, installed);

    check(launched && !launched->running
          && launched->path == "/Applications/CowTerm.app");
    check(!resolveSlotTarget(makePaneSlot("main"), {}, {}));
};

auto tUrlSlot =
    test("Slots/a url slot switches to the tab showing it, else opens it") = []
{
    auto apps = std::vector<RudderItem> {
        makeRunningApp(grazer, "Grazer", {makeTab("7", "https://github.com/")})};

    auto open = resolveSlotTarget(makeUrlSlot("https://github.com"), apps, {});
    check(open && open->kind == "context" && open->contextId == "7");

    auto fresh = resolveSlotTarget(makeUrlSlot("https://example.com"), apps, {});
    check(fresh && fresh->kind == "favourite"
          && fresh->url == "https://example.com");
};

auto tSlotChords = test("Slots/a key press finds its slot chord, jump or put") = []
{
    auto bindings = Keybindings {};
    bindings.jumpToSlot = {"control+command+y", "control+command+u"};
    bindings.putInSlot = {"control+shift+command+y", "control+shift+command+u"};

    check(findSlotPress(bindings, keyCodeU, {.control = true, .command = true})
          == SlotPress {SlotChordAction::jump, 1});
    check(findSlotPress(
              bindings, keyCodeY, {.control = true, .shift = true, .command = true})
          == SlotPress {SlotChordAction::put, 0});
    check(!findSlotPress(bindings, keyCodeU, {.control = true}));
};

auto tSlotChordOwner = test("Slots/a chord already bound to a slot says which") = []
{
    auto bindings = Keybindings {};
    bindings.jumpToSlot = {"control+command+y", "control+command+u"};
    bindings.putInSlot = {"control+shift+command+y", "control+shift+command+u"};

    check(describeSlotChordOwner(bindings, *parseChord("cmd+ctrl+u")) == "slot 2");
    check(describeSlotChordOwner(bindings, *parseChord("cmd+ctrl+shift+y"))
          == "putting in slot 1");
    check(!describeSlotChordOwner(bindings, *parseChord("cmd+ctrl+p")));
};

auto tFrontAppSlot =
    test("Slots/where you are is the front app's focused pane, else the app") = []
{
    auto focused = makePane("pane:2", "main");
    focused.focused = true;
    auto nameFor = [](const std::string& key)
    {
        return key == std::string {cowTerm} + "/repo:" + rudderRepo + "#branch:main"
                   ? "mine"
                   : "";
    };

    auto pane = makeSlotForFrontApp(
        cowTerm, "CowTerm", {makePane("pane:1", "fix-login"), focused}, nameFor);
    check(pane.branch == "main" && pane.title == "mine");

    auto app = makeSlotForFrontApp(
        cowTerm, "CowTerm", {makePane("pane:1", "fix-login")}, nameFor);
    check(app == makeSlotForApp(cowTerm, "CowTerm"));
};

auto tRowLabels = test("Slots/a row in a slot is labelled with its slot's key") = []
{
    auto slots = std::vector<Slot> {};
    assignSlot(slots, 0, makeSlotForApp(cowTerm, "CowTerm"));
    assignSlot(slots, 2, makeUrlSlot("https://github.com"));

    auto chords = Keybindings {}.jumpToSlot;
    chords[2] = "";
    auto favourite = RudderItem {};
    favourite.kind = "favourite";
    favourite.url = "https://github.com/";

    auto labels = labelRowsWithSlotKeys(slots,
                                        chords,
                                        {makeRunningApp(cowTerm, "CowTerm"),
                                         makeRunningApp(grazer, "Grazer"),
                                         favourite});

    check(labels == std::vector<std::string> {"Y", "", "3"});
};
