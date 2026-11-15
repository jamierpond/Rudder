#include <SearchKeys/SearchKeys.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace searchkeys;

namespace
{
Key makeTypedKey(const std::string& characters)
{
    auto key = Key {};
    key.characters = characters;
    return key;
}

Key makeSpecialKey(SpecialKey special)
{
    auto key = Key {};
    key.special = special;
    return key;
}

State makeNormalState(const std::string& text, int caret)
{
    auto state = State {};
    state.mode = Mode::normal;
    state.text = text;
    state.caret = caret;
    return state;
}

Action pressInNormal(const std::string& characters)
{
    return handleKey(makeNormalState("git", 1), makeTypedKey(characters)).action;
}

Result pressSequence(State state, const std::vector<std::string>& keys)
{
    auto result = Result {state, Action::none};

    for (auto& characters: keys)
        result = handleKey(result.state, makeTypedKey(characters));

    return result;
}
} // namespace

auto tSearchKeysOpensInInsert = test("SearchKeys/opens in insert mode, typing passes through") = []
{
    auto state = getOpeningState();
    check(state.mode == Mode::insert);
    check(handleKey(state, makeTypedKey("j")).action == Action::passThrough);
    check(handleKey(state, makeSpecialKey(SpecialKey::enter)).action == Action::passThrough);
};

auto tSearchKeysEscapeToNormal = test("SearchKeys/Esc in insert enters normal, text and caret kept") = []
{
    auto state = getOpeningState();
    state.text = "git";
    state.caret = 2;

    auto result = handleKey(state, makeSpecialKey(SpecialKey::escape));
    check(result.action == Action::none);
    check(result.state == makeNormalState("git", 2));
};

auto tSearchKeysEscapeInNormalCloses = test("SearchKeys/Esc in normal closes the search") = []
{
    auto result = handleKey(makeNormalState("git", 1), makeSpecialKey(SpecialKey::escape));
    check(result.action == Action::closeSearch);
};

auto tSearchKeysMoves = test("SearchKeys/j, k, arrows, gg and G move the highlight") = []
{
    check(pressInNormal("j") == Action::moveHighlightDown);
    check(pressInNormal("k") == Action::moveHighlightUp);
    check(pressInNormal("G") == Action::moveHighlightToLast);
    check(pressSequence(makeNormalState("", 0), {"g", "g"}).action
          == Action::moveHighlightToFirst);

    auto normal = makeNormalState("", 0);
    check(handleKey(normal, makeSpecialKey(SpecialKey::downArrow)).action
          == Action::moveHighlightDown);
    check(handleKey(normal, makeSpecialKey(SpecialKey::upArrow)).action
          == Action::moveHighlightUp);
};

auto tSearchKeysEnterOpens = test("SearchKeys/Enter opens, command-Enter opens in a new tab") = []
{
    auto normal = makeNormalState("git", 1);
    auto enter = makeSpecialKey(SpecialKey::enter);
    check(handleKey(normal, enter).action == Action::openHighlighted);

    enter.command = true;
    check(handleKey(normal, enter).action == Action::openHighlightedInNewTab);
};

auto tSearchKeysBackToInsert = test("SearchKeys/i, a and / insert at the caret, A at the end, I at the start") = []
{
    for (auto characters: {"i", "a", "/"})
    {
        auto result = handleKey(makeNormalState("git", 1), makeTypedKey(characters));
        check(result.state.mode == Mode::insert);
        check(result.state.caret == 1);
        check(result.state.text == "git");
    }

    check(handleKey(makeNormalState("git", 1), makeTypedKey("A")).state.caret == 3);
    check(handleKey(makeNormalState("git", 1), makeTypedKey("I")).state.caret == 0);
};

auto tSearchKeysCloseRow = test("SearchKeys/dd and x close the highlighted row") = []
{
    check(pressInNormal("x") == Action::closeHighlighted);

    auto afterFirst = handleKey(makeNormalState("git", 1), makeTypedKey("d"));
    check(afterFirst.action == Action::none);
    check(afterFirst.state.pending == PendingOperator::deleteRow);

    auto afterSecond = handleKey(afterFirst.state, makeTypedKey("d"));
    check(afterSecond.action == Action::closeHighlighted);
    check(afterSecond.state.pending == PendingOperator::none);
};

auto tSearchKeysStar = test("SearchKeys/s stars the highlighted row") = []
{
    check(pressInNormal("s") == Action::starHighlighted);
};

auto tSearchKeysChange = test("SearchKeys/cc and S clear the text and enter insert") = []
{
    auto expected = getOpeningState();

    check(pressSequence(makeNormalState("git", 1), {"c", "c"}).state == expected);
    check(handleKey(makeNormalState("git", 1), makeTypedKey("S")).state == expected);
};

auto tSearchKeysOperatorCancels = test("SearchKeys/an operator followed by another key does nothing") = []
{
    auto start = makeNormalState("git", 1);

    for (auto sequence: std::vector<std::vector<std::string>> {
             {"d", "j"}, {"g", "k"}, {"c", "x"}, {"d", "g"}})
    {
        auto result = pressSequence(start, sequence);
        check(result.action == Action::none);
        check(result.state == start);
    }

    auto pending = handleKey(start, makeTypedKey("d")).state;
    auto escaped = handleKey(pending, makeSpecialKey(SpecialKey::escape));
    check(escaped.action == Action::none);
    check(escaped.state == start);
};

auto tSearchKeysUnknownKeys = test("SearchKeys/unknown keys in normal do nothing and never type") = []
{
    auto start = makeNormalState("git", 1);

    for (auto characters: {"q", "z", "1", " ", "é"})
    {
        auto result = handleKey(start, makeTypedKey(characters));
        check(result.action == Action::none);
        check(result.state == start);
    }

    auto alted = makeTypedKey("j");
    alted.alt = true;
    check(handleKey(start, alted).action == Action::none);
};

auto tSearchKeysChordsPassThrough = test("SearchKeys/control and command chords stay the app's in normal") = []
{
    auto start = makeNormalState("git", 1);

    auto controlN = makeTypedKey("n");
    controlN.control = true;
    check(handleKey(start, controlN) == Result {start, Action::passThrough});

    auto commandD = makeTypedKey("d");
    commandD.command = true;
    check(handleKey(start, commandD).action == Action::passThrough);
};
