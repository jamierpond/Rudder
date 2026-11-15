#include <SearchKeys/SearchKeys.h>

#include <NanoTest/NanoTest.h>

#include <initializer_list>

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

Action pressKeys(SearchBox& box, std::initializer_list<std::string> keys)
{
    auto last = Action::none;

    for (auto& characters: keys)
        last = box.handleKey(makeTypedKey(characters));

    return last;
}

Action pressEscape(SearchBox& box)
{
    return box.handleKey(makeSpecialKey(SpecialKey::escape));
}

// Typed in insert mode, then Esc: normal mode with the caret on the last character.
SearchBox makeNormalBox(const std::string& text)
{
    auto box = SearchBox {};
    box.handleKey(makeTypedKey(text));
    pressEscape(box);
    return box;
}
} // namespace

auto tSearchKeysOpensInInsert = test("SearchKeys/opens in insert mode, typing edits the text") = []
{
    auto box = SearchBox {};
    check(!box.isNormalMode());

    check(pressKeys(box, {"g", "i", "t"}) == Action::none);
    check(box.getText() == "git");
    check(box.getCaret() == 3);
};

auto tSearchKeysEscapeOnLastCharacter = test("SearchKeys/Esc in insert puts the caret on the last character") = []
{
    auto box = makeNormalBox("hello");
    check(box.isNormalMode());
    check(box.getText() == "hello");
    check(box.getCaret() == 4);
};

auto tSearchKeysWordBack = test("SearchKeys/b from the end of hello world goes to world") = []
{
    constexpr auto startOfWorld = 6;
    auto box = makeNormalBox("hello world");

    pressKeys(box, {"b"});
    check(box.getCaret() == startOfWorld);

    pressKeys(box, {"b"});
    check(box.getCaret() == 0);
};

auto tSearchKeysEditing = test("SearchKeys/x, dw, ciw, dd and u edit the text") = []
{
    auto box = makeNormalBox("hello world");

    pressKeys(box, {"0", "x"});
    check(box.getText() == "ello world");

    pressKeys(box, {"d", "w"});
    check(box.getText() == "world");

    pressKeys(box, {"u"});
    check(box.getText() == "ello world");

    pressKeys(box, {"c", "i", "w"});
    check(!box.isNormalMode());
    pressKeys(box, {"h", "i"});
    check(box.getText() == "hi world");

    pressEscape(box);
    pressKeys(box, {"d", "d"});
    check(box.getText().empty());
};

auto tSearchKeysEscapeInNormalCloses = test("SearchKeys/Esc in normal closes the search") = []
{
    auto box = makeNormalBox("git");
    check(pressEscape(box) == Action::closeSearch);
};

auto tSearchKeysEscapeCancelsOperator = test("SearchKeys/Esc after an operator cancels it, not the search") = []
{
    auto box = makeNormalBox("git");
    pressKeys(box, {"d"});
    check(pressEscape(box) == Action::none);
    check(pressEscape(box) == Action::closeSearch);
};

auto tSearchKeysMoves = test("SearchKeys/j, k, arrows, gg and G move the highlight") = []
{
    auto box = makeNormalBox("git");

    check(pressKeys(box, {"j"}) == Action::moveHighlightDown);
    check(pressKeys(box, {"k"}) == Action::moveHighlightUp);
    check(pressKeys(box, {"G"}) == Action::moveHighlightToLast);
    check(pressKeys(box, {"g", "g"}) == Action::moveHighlightToFirst);
    check(box.handleKey(makeSpecialKey(SpecialKey::downArrow)) == Action::moveHighlightDown);
    check(box.handleKey(makeSpecialKey(SpecialKey::upArrow)) == Action::moveHighlightUp);
    check(box.getText() == "git");
};

auto tSearchKeysArrowsInInsert = test("SearchKeys/up and down arrows move the highlight in insert too") = []
{
    auto box = SearchBox {};
    check(box.handleKey(makeSpecialKey(SpecialKey::downArrow)) == Action::moveHighlightDown);
    check(!box.isNormalMode());
};

auto tSearchKeysOperatorWithMotion = test("SearchKeys/after an operator, j is its motion, not a highlight move") = []
{
    auto box = makeNormalBox("git");
    pressKeys(box, {"d"});
    check(pressKeys(box, {"j"}) == Action::none);
};

auto tSearchKeysEnterOpens = test("SearchKeys/Enter opens, command-Enter opens in a new tab") = []
{
    auto box = makeNormalBox("git");
    check(box.handleKey(makeSpecialKey(SpecialKey::enter)) == Action::openHighlighted);

    auto commandEnter = makeSpecialKey(SpecialKey::enter);
    commandEnter.command = true;
    check(box.handleKey(commandEnter) == Action::openHighlightedInNewTab);

    auto insertBox = SearchBox {};
    check(insertBox.handleKey(makeSpecialKey(SpecialKey::enter)) == Action::openHighlighted);
};

auto tSearchKeysStar = test("SearchKeys/s stars the highlighted row") = []
{
    auto box = makeNormalBox("git");
    check(pressKeys(box, {"s"}) == Action::starHighlighted);
    check(box.getText() == "git");
};

auto tSearchKeysStaysOneLine = test("SearchKeys/o and yyp never make a second line") = []
{
    auto box = makeNormalBox("git");

    pressKeys(box, {"o"});
    check(box.getText() == "git");

    pressKeys(box, {"y", "y", "p"});
    check(box.getText() == "git");
};

auto tSearchKeysChordsPassThrough = test("SearchKeys/control and command chords stay the app's") = []
{
    auto box = makeNormalBox("git");

    auto controlW = makeTypedKey("w");
    controlW.control = true;
    check(box.handleKey(controlW) == Action::passThrough);
    check(box.getText() == "git");

    auto commandD = makeTypedKey("d");
    commandD.command = true;
    check(box.handleKey(commandD) == Action::passThrough);
    check(box.getText() == "git");
};

auto tSearchKeysAppEditsKeepMode = test("SearchKeys/the app's own edits keep the mode") = []
{
    auto box = SearchBox {};
    box.setTextAndCaret("github", 3);
    check(!box.isNormalMode());
    check(box.getCaret() == 3);

    pressEscape(box);
    box.setTextAndCaret("git", 3);
    check(box.isNormalMode());
    check(box.getCaret() == 2);
};

auto tSearchKeysControlWDeletesWord = test("SearchKeys/ctrl+w in insert mode deletes the word behind the caret") = []
{
    auto box = SearchBox {};
    pressKeys(box, {"g", "i", "t", " ", "l", "o", "g"});

    auto controlW = makeTypedKey("w");
    controlW.control = true;
    check(box.handleKey(controlW) == Action::none);
    check(box.getText() == "git ");
    check(!box.isNormalMode());
};
