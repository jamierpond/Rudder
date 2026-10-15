#include <LineEdit.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
// "|" marks the cursor in the text, which keeps each case on one line.
Line at(const std::string& marked)
{
    auto bar = marked.find('|');
    auto text = marked;
    text.erase(bar, 1);
    return {text, (int) bar};
}

std::string show(const Line& line)
{
    auto text = line.text;
    text.insert((std::size_t) line.cursor, "|");
    return text;
}
} // namespace

auto tMoves = test("LineEdit/ctrl+a, ctrl+e, ctrl+b, ctrl+f move") = []
{
    check(show(line::start(at("abc|d"))) == "|abcd");
    check(show(line::end(at("a|bcd"))) == "abcd|");
    check(show(line::left(at("ab|c"))) == "a|bc");
    check(show(line::right(at("ab|c"))) == "abc|");
    check(show(line::left(at("|abc"))) == "|abc");
    check(show(line::right(at("abc|"))) == "abc|");
};

auto tWordMoves = test("LineEdit/alt+b and alt+f move by word") = []
{
    check(show(line::wordLeft(at("visual studio code|"))) == "visual studio |code");
    check(show(line::wordLeft(at("visual studio |code"))) == "visual |studio code");
    check(show(line::wordRight(at("|visual studio"))) == "visual| studio");
    check(show(line::wordRight(at("visual| studio"))) == "visual studio|");
    check(show(line::wordLeft(at("foo-bar|"))) == "foo-|bar");
};

auto tDeletes = test("LineEdit/ctrl+h and ctrl+d delete a character") = []
{
    check(show(line::backspace(at("abc|"))) == "ab|");
    check(show(line::backspace(at("|abc"))) == "|abc");
    check(show(line::deleteForward(at("a|bc"))) == "a|c");
    check(show(line::deleteForward(at("abc|"))) == "abc|");
};

auto tCtrlW = test("LineEdit/ctrl+w deletes back to whitespace") = []
{
    check(show(line::deleteWhitespaceWord(at("grazer github.com|"))) == "grazer |");
    check(show(line::deleteWhitespaceWord(at("grazer github  |"))) == "grazer |");
    check(show(line::deleteWhitespaceWord(at("grazer| github"))) == "| github");
};

auto tAltBackspace = test("LineEdit/alt+backspace deletes one word") = []
{
    check(show(line::deleteWord(at("grazer github.com|"))) == "grazer github.|");
    check(show(line::deleteWord(at("foo bar |"))) == "foo |");
};

auto tKillLine = test("LineEdit/ctrl+u and ctrl+k delete to either end") = []
{
    check(show(line::deleteToStart(at("foo b|ar"))) == "|ar");
    check(show(line::deleteToEnd(at("foo b|ar"))) == "foo b|");
};

auto tUtf8 = test("LineEdit/moves never split a UTF-8 character") = []
{
    auto text = Line {"caf\xC3\xA9", 5};

    check(line::left(text).cursor == 3);
    check(line::backspace(text).text == "caf");
    check(line::right(Line {text.text, 3}).cursor == 5);
};

auto tInsert = test("LineEdit/typing inserts at the cursor") = []
{
    check(show(line::insert(at("ab|d"), "c")) == "abc|d");
    check(show(line::insert(at("|"), "é")) == "é|");
};

auto tScroll = test("LineEdit/a long line scrolls to keep the caret in view") = []
{
    // Fits: no scroll.
    check(line::scrollToCaret(40.f, 80.f, 100.f, 0.f) == 0.f);
    // The caret at the end of a long title: its tail shows.
    check(line::scrollToCaret(300.f, 300.f, 100.f, 0.f) == 200.f);
    // Moving back inside the view keeps the scroll; past its left edge follows.
    check(line::scrollToCaret(250.f, 300.f, 100.f, 200.f) == 200.f);
    check(line::scrollToCaret(150.f, 300.f, 100.f, 200.f) == 150.f);
    // Deleting pulls the text back so it still fills the view.
    check(line::scrollToCaret(120.f, 120.f, 100.f, 200.f) == 20.f);
};
