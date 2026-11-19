#include "PanelView.h"

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace eacp;
using rudder::PanelView;

namespace
{
// macOS virtual key codes for the letters the tests type.
constexpr std::uint16_t keyCodeB = 0x0B;
constexpr std::uint16_t keyCodeX = 0x07;
constexpr std::uint16_t keyCodeForOtherLetters = 0x00;

Graphics::KeyEvent makeLetterKey(char letter)
{
    auto event = Graphics::KeyEvent {};
    event.characters = std::string(1, letter);
    event.charactersIgnoringModifiers = event.characters;
    event.keyCode = keyCodeForOtherLetters;

    if (letter == 'b')
        event.keyCode = keyCodeB;
    else if (letter == 'x')
        event.keyCode = keyCodeX;

    return event;
}

Graphics::KeyEvent makeEscapeKey()
{
    auto event = Graphics::KeyEvent {};
    event.keyCode = Graphics::KeyCode::Escape;
    return event;
}

void typeText(PanelView& panel, const std::string& text)
{
    for (auto letter: text)
        panel.keyDown(makeLetterKey(letter));
}
} // namespace

auto tPanelEscapeOnLastCharacter = test("PanelKeys/Esc after typing puts the caret on the last character") = []
{
    auto panel = PanelView {};
    panel.reset(false);
    typeText(panel, "hello");
    check(panel.getQueryText() == "hello");

    panel.keyDown(makeEscapeKey());
    check(panel.getQueryCaret() == 4);
};

auto tPanelWordBack = test("PanelKeys/b from the end of hello world goes back a word") = []
{
    constexpr auto startOfWorld = 6;
    auto panel = PanelView {};
    panel.reset(false);
    typeText(panel, "hello world");
    panel.keyDown(makeEscapeKey());

    panel.keyDown(makeLetterKey('b'));
    check(panel.getQueryCaret() == startOfWorld);

    panel.keyDown(makeLetterKey('x'));
    check(panel.getQueryText() == "hello orld");
};

auto tPanelControlWDeletesWord = test("PanelKeys/ctrl+w in insert mode deletes the word behind the caret") = []
{
    auto panel = PanelView {};
    panel.reset(false);
    typeText(panel, "hello world");

    auto controlW = Graphics::KeyEvent {};
    controlW.characters = "\x17";
    controlW.charactersIgnoringModifiers = "w";
    controlW.keyCode = Graphics::KeyCode::W;
    controlW.modifiers.control = true;
    panel.keyDown(controlW);

    check(panel.getQueryText() == "hello ");
};

auto tPanelCycleChords = test(
    "PanelKeys/the cycle chord and its Shift reverse reach the app, not the query") =
    []
{
    auto panel = PanelView {};
    auto settings = rudder::SettingsModel {};
    settings.cycle = "control+tab";
    panel.setSettings(settings);
    panel.reset(false);

    auto forwardPresses = 0;
    auto reversePresses = 0;
    panel.onCycleChord = [&] { ++forwardPresses; };
    panel.onReverseCycleChord = [&] { ++reversePresses; };

    auto controlTab = Graphics::KeyEvent {};
    controlTab.characters = "\t";
    controlTab.charactersIgnoringModifiers = "\t";
    controlTab.keyCode = Graphics::KeyCode::Tab;
    controlTab.modifiers.control = true;
    panel.keyDown(controlTab);

    auto controlShiftTab = controlTab;
    controlShiftTab.modifiers.shift = true;
    panel.keyDown(controlShiftTab);

    check(forwardPresses == 1);
    check(reversePresses == 1);
    check(panel.getQueryText().empty());
};
