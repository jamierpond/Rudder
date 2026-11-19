#include "TestSupport.h"

#include <Keybinding.h>
#include <Slots.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Utils/Files.h>

using namespace nano;
using namespace rudder;

auto tParsesDefaults = test("Keybinding/the defaults parse") = []
{
    auto summon = parseChord(Keybindings {}.summon);
    auto back = parseChord(Keybindings {}.back);

    check(summon.has_value() && summon->option && summon->key == "space");
    check(back.has_value() && back->option && back->key == "`");
};

auto tAliases = test("Keybinding/modifier aliases and case are accepted") = []
{
    auto chord = parseChord("Cmd+Shift+Alt+Ctrl+K");

    check(chord.has_value());
    check(chord->command && chord->shift && chord->option && chord->control);
    check(chord->key == "k");
};

auto tRoundTrip = test("Keybinding/serialise is canonical and round-trips") = []
{
    auto chord = parseChord("shift+cmd+opt+p");

    check(serialiseChord(*chord) == "option+shift+command+p");
    check(parseChord(serialiseChord(*chord)) == chord);
};

auto tRejects = test("Keybinding/bad chords are rejected") = []
{
    check(!parseChord("").has_value());
    check(!parseChord("option+").has_value());
    check(!parseChord("option+nope").has_value());
    check(!parseChord("hyper+k").has_value());

    // A bare letter would eat ordinary typing system-wide.
    check(!parseChord("k").has_value());
    check(parseChord("f5").has_value());
};

auto tDisplay = test("Keybinding/display uses the platform's menu labels") = []
{
    if constexpr (eacp::Platform::isWindows())
    {
        check(displayChord(*parseChord("option+space")) == "Alt+Space");
        check(displayChord(*parseChord("control+shift+k")) == "Ctrl+Shift+K");
        check(displayChord(*parseChord("win+d")) == "Win+D");
    }
    else
    {
        check(displayChord(*parseChord("option+space")) == "⌥Space");
        check(displayChord(*parseChord("control+shift+k")) == "⌃⇧K");
    }
};

auto tKeyCodes = test("Keybinding/key names map to macOS key codes and back") = []
{
    check(keyCodeFor("space") == 0x31);
    check(keyCodeFor("`") == 0x32);
    check(keyCodeFor("a") == 0x00);
    check(!keyCodeFor("nope").has_value());

    check(keyNameFor(0x31) == "space");
    check(keyNameFor(0x32) == "`");
    check(!keyNameFor(0x3A).has_value());
};

auto tStored = test("Keybinding/bindings persist in emberstore") = []
{
    {
        auto db = testing::freshDatabase("keybindings");
        auto store = db.collection<Keybindings>("settings");
        store.doc("keybindings").set({"control+space", "option+tab"});
    }

    auto db = emberstore::Database {
        (std::filesystem::temp_directory_path() / "RudderTests" / "keybindings")
            .string()};
    auto stored = db.collection<Keybindings>("settings").get("keybindings");

    check(stored.has_value());
    check(stored->summon == "control+space");
    check(stored->back == "option+tab");
};

auto tMatches = test("Keybinding/a press matches only its exact chord") = []
{
    auto quit = *parseChord(Keybindings {}.quit);

    // control+q by default; ⌘Q is not it.
    check(matchesChord(quit, 0x0C, true, false, false, false));
    check(!matchesChord(quit, 0x0C, true, false, true, false));
    check(!matchesChord(quit, 0x0C, false, false, false, true));
    check(!matchesChord(quit, 0x0D, true, false, false, false));
};

auto tOldDocumentsGetQuit = test("Keybinding/a document from before quit gets the default")
    = []
{
    auto dir = std::filesystem::temp_directory_path() / "RudderTests" / "keybindings-old";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    eacp::Files::writeFile(
        dir / "settings.json",
        R"({"keybindings": {"summon": "control+space", "back": "option+tab"}})");

    auto db = emberstore::Database {dir.string()};
    auto stored = db.collection<Keybindings>("settings").get("keybindings");

    check(stored.has_value());
    check(stored->summon == "control+space");
    check(stored->quit == "control+q");
    check(stored->cycle == Keybindings {}.cycle);

    addMissingSlotChords(*stored);
    check(stored->jumpToSlot == Keybindings {}.jumpToSlot);
    check(stored->putInSlot == Keybindings {}.putInSlot);
};

auto tCycleDefault = test("Keybinding/the cycle default parses as control+tab") = []
{
    auto cycle = parseChord(Keybindings {}.cycle);

    check(cycle.has_value() && cycle->control && cycle->key == "tab");
    check(!cycle->option && !cycle->shift && !cycle->command);
};

auto tAnyChordModifierHeld = test(
    "Keybinding/a chord's modifiers count as held while any one of them is") = []
{
    auto chord = *parseChord("control+option+tab");

    check(isAnyChordModifierHeld(chord, {.control = true}));
    check(isAnyChordModifierHeld(chord, {.option = true}));
    check(!isAnyChordModifierHeld(chord, {.shift = true, .command = true}));
    check(!isAnyChordModifierHeld(chord, {}));
};

auto tReverseCycleChord = test(
    "Keybinding/the reverse cycle chord adds Shift, unless the chord has it") = []
{
    auto reverse = getReverseCycleChord(*parseChord("control+tab"));

    check(reverse.has_value() && reverse->control && reverse->shift
          && reverse->key == "tab");
    check(!getReverseCycleChord(*parseChord("control+shift+tab")).has_value());
};

auto tMissingSlotChords = test(
    "Keybinding/slots past the end of a stored chord list get their default chord") =
    []
{
    auto bindings = Keybindings {};
    bindings.jumpToSlot = {"control+option+1", ""};
    addMissingSlotChords(bindings);

    check(bindings.jumpToSlot.size() == Keybindings {}.jumpToSlot.size());
    check(bindings.jumpToSlot[0] == "control+option+1");
    check(bindings.jumpToSlot[1].empty());
    check(bindings.jumpToSlot[2] == Keybindings {}.jumpToSlot[2]);
    check(bindings.putInSlot == Keybindings {}.putInSlot);
};

auto tSlotChordDefaults = test(
    "Keybinding/each slot's default put chord is its jump chord with Shift") = []
{
    auto bindings = Keybindings {};

    check(bindings.jumpToSlot.size() == slotCount
          && bindings.putInSlot.size() == slotCount);

    for (std::size_t index = 0; index < bindings.jumpToSlot.size(); ++index)
    {
        auto jump = parseChord(bindings.jumpToSlot[index]);
        auto put = parseChord(bindings.putInSlot[index]);

        check(jump && put && jump->control && !jump->shift);

        if (!jump || !put)
            continue;

        jump->shift = true;
        check(*jump == *put);
    }

    check(bindings.jumpToSlot.front()
          == serialiseChord(*parseChord(bindings.jumpToSlot.front())));
};
