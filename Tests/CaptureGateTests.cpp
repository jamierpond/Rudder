#include <CaptureGate.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace nano;
using namespace rudder;

namespace
{
constexpr auto second = std::uint64_t {1'000'000'000};

// Option+Space, the default summon.
const auto summon = CaptureKey {49, captureModifiers::option};
const auto letterA = CaptureKey {0, 0};
const auto hotkeys = std::array {summon};

const auto open = CaptureState {true, true, false};

CaptureVerdict keyDown(const CaptureState& state, CaptureKey key)
{
    return decideCapture(CaptureEvent::keyDown, state, key, hotkeys);
}
} // namespace

auto tSwallowsTyping = test("CaptureGate/typing into the open panel is swallowed") = []
{
    check(keyDown(open, letterA) == CaptureVerdict::swallow);
    check(decideCapture(CaptureEvent::keyUp, open, letterA, hotkeys)
          == CaptureVerdict::swallow);
};

auto tHotkeysPass = test("CaptureGate/the global hotkeys pass through") = []
{
    check(keyDown(open, summon) == CaptureVerdict::pass);
    check(decideCapture(CaptureEvent::keyUp, open, summon, hotkeys)
          == CaptureVerdict::pass);

    // Same key, other modifiers: typing, so swallowed.
    check(keyDown(open, {49, 0}) == CaptureVerdict::swallow);
    check(keyDown(open, {49, captureModifiers::option | captureModifiers::shift})
          == CaptureVerdict::swallow);
};

auto tRecordingTakesHotkeys = test("CaptureGate/recording a chord swallows the hotkeys too") = []
{
    check(decideCapture(CaptureEvent::keyDown, open, summon, {})
          == CaptureVerdict::swallow);
};

auto tTimeoutAborts = test("CaptureGate/an OS timeout aborts and is never undone") = []
{
    check(decideCapture(CaptureEvent::tapTimedOut, open, std::nullopt, hotkeys)
          == CaptureVerdict::passAndAbort);
    check(decideCapture(CaptureEvent::tapTimedOut, {}, std::nullopt, hotkeys)
          == CaptureVerdict::passAndAbort);
};

auto tOwnDisableIgnored = test("CaptureGate/the notice of Rudder's own disable is ignored") = []
{
    // It lands after the next start: aborting on it closed every fresh panel.
    check(decideCapture(CaptureEvent::tapDisabledByUser, open, std::nullopt, hotkeys)
          == CaptureVerdict::pass);
    check(decideCapture(CaptureEvent::tapDisabledByUser, {}, std::nullopt, hotkeys)
          == CaptureVerdict::pass);
};

auto tNoPanelPasses = test("CaptureGate/no panel on screen: everything passes, tap off") = []
{
    check(keyDown({true, false, false}, letterA) == CaptureVerdict::passAndStop);
    check(keyDown({false, true, false}, letterA) == CaptureVerdict::passAndStop);
    check(keyDown({false, false, false}, letterA) == CaptureVerdict::passAndStop);
    check(decideCapture(CaptureEvent::other, {true, false, false}, std::nullopt, hotkeys)
          == CaptureVerdict::passAndStop);
};

auto tStallAborts = test("CaptureGate/a stuck main thread gets the keyboard back") = []
{
    check(keyDown({true, true, true}, letterA) == CaptureVerdict::passAndAbort);
    check(keyDown({true, true, true}, summon) == CaptureVerdict::passAndAbort);
};

auto tOtherEventsPass = test("CaptureGate/anything but a key passes") = []
{
    check(decideCapture(CaptureEvent::other, open, std::nullopt, hotkeys)
          == CaptureVerdict::pass);
    check(decideCapture(CaptureEvent::keyDown, open, std::nullopt, hotkeys)
          == CaptureVerdict::pass);
};

auto tStallNeedsBacklog = test("CaptureGate/idle is never a stall") = []
{
    // Nothing waiting, however long since the last key.
    check(!mainStalled({5, 5, 0}, 3600 * second));
    check(!mainStalled({0, 0, 0}, 3600 * second));
};

auto tStallAfterCap = test("CaptureGate/a key unhandled past the cap is a stall") = []
{
    auto backlog = CaptureBacklog {3, 2, 10 * second};

    check(!mainStalled(backlog, 10 * second));
    check(!mainStalled(backlog, 10 * second + captureStallNanos));
    check(mainStalled(backlog, 10 * second + captureStallNanos + 1));
    check(mainStalled(backlog, 60 * second));

    // A clock read before the progress stamp landed: no stall.
    check(!mainStalled(backlog, 9 * second));
};

auto tChordKeys = test("CaptureGate/chords map to the tap's keys") = []
{
    auto chord = parseChord("option+space");
    check(chord.has_value());
    check(captureKeyFor(*chord) == std::optional {summon});

    auto all = parseChord("control+option+shift+command+k");
    check(all.has_value());
    auto key = captureKeyFor(*all);
    check(key.has_value() && key->keyCode == 40 && key->modifiers == 15);
};

namespace
{
constexpr std::uint8_t cmdShift = captureModifiers::command | captureModifiers::shift;
const auto shot4 = CaptureKey {21, cmdShift};
const auto shotKeys = std::array {CaptureKey {20, cmdShift}, shot4};
const auto spaceKey = CaptureKey {49, 0};
const auto escapeKey = CaptureKey {53, 0};
const auto screenshotting = CaptureState {true, true, false, true};

bool hasChord(const std::vector<CaptureKey>& chords, CaptureKey key)
{
    return std::find(chords.begin(), chords.end(), key) != chords.end();
}
} // namespace

auto tScreenshotChordPasses = test("CaptureGate/a screenshot chord reaches the system") = []
{
    check(decideCapture(CaptureEvent::keyDown, open, shot4, hotkeys, shotKeys)
          == CaptureVerdict::passAndScreenshot);
    check(decideCapture(CaptureEvent::keyUp, open, shot4, hotkeys, shotKeys)
          == CaptureVerdict::passAndScreenshot);

    // Recording a chord still lets a screenshot of the panel happen.
    check(decideCapture(CaptureEvent::keyDown, open, shot4, {}, shotKeys)
          == CaptureVerdict::passAndScreenshot);

    // Without the list it is just typing.
    check(keyDown(open, shot4) == CaptureVerdict::swallow);
};

auto tScreenshotKeysPass = test("CaptureGate/keys during a screenshot are the system's") = []
{
    for (auto key: {spaceKey, escapeKey, letterA, CaptureKey {123, 0}})
    {
        check(decideCapture(CaptureEvent::keyDown, screenshotting, key, hotkeys, shotKeys)
              == CaptureVerdict::pass);
        check(decideCapture(CaptureEvent::keyUp, screenshotting, key, hotkeys, shotKeys)
              == CaptureVerdict::pass);
    }

    // Once it is over, the panel has the keyboard again.
    check(decideCapture(CaptureEvent::keyDown, open, escapeKey, hotkeys, shotKeys)
          == CaptureVerdict::swallow);

    // The safety nets still win.
    check(decideCapture(CaptureEvent::tapTimedOut, screenshotting, std::nullopt, hotkeys, shotKeys)
          == CaptureVerdict::passAndAbort);
    check(decideCapture(CaptureEvent::keyDown, {true, false, false, true}, spaceKey, hotkeys, shotKeys)
          == CaptureVerdict::passAndStop);
};

auto tScreenshotDefaults = test("CaptureGate/the screenshot chords default to macOS's") = []
{
    auto chords = screenshotChords({});
    constexpr std::uint8_t ctrlCmdShift = cmdShift | captureModifiers::control;

    check(chords.size() == 6);
    check(hasChord(chords, {20, cmdShift}));
    check(hasChord(chords, {20, ctrlCmdShift}));
    check(hasChord(chords, {21, cmdShift}));
    check(hasChord(chords, {21, ctrlCmdShift}));
    check(hasChord(chords, {23, cmdShift}));
    check(hasChord(chords, {22, cmdShift}));
};

auto tScreenshotUserChords = test("CaptureGate/the user's screenshot shortcuts win") = []
{
    // ⌘⇧4 moved to ⌥⌘4, ⌘⇧3 switched off, ⌘⇧5 with no value, ⌘⇧6 with no key.
    auto entries = std::array {
        SymbolicHotKey {30, true, std::uint16_t {21}, (1 << 19) | (1 << 20)},
        SymbolicHotKey {28, false, std::uint16_t {20}, (1 << 17) | (1 << 20)},
        SymbolicHotKey {184, true, std::nullopt, 0},
        SymbolicHotKey {181, true, std::uint16_t {0xFFFF}, 0},
    };

    auto chords = screenshotChords(entries);

    check(chords.size() == 4);
    check(hasChord(chords, {21, captureModifiers::option | captureModifiers::command}));
    check(!hasChord(chords, {21, cmdShift}));
    check(!hasChord(chords, {20, cmdShift}));
    check(hasChord(chords, {23, cmdShift}));
    check(!hasChord(chords, {22, cmdShift}));
};

auto tScreenshotSession = test("CaptureGate/a screenshot lasts as long as its UI") = []
{
    auto session = ScreenshotSession {10 * second};

    // Waiting for the UI to show: on through the grace, then over.
    check(screenshotGoing(session, false, 10 * second));
    check(screenshotGoing(session, false, 10 * second + screenshotGraceNanos / 2));
    check(!screenshotGoing(session, false, 10 * second + screenshotGraceNanos));

    // Shown: on however long the drag takes, over the moment it goes.
    session = ScreenshotSession {10 * second};
    check(screenshotGoing(session, true, 10 * second + second / 10));
    check(screenshotGoing(session, true, 70 * second));
    check(!screenshotGoing(session, false, 70 * second + second / 10));

    // A quick one (⌘⇧3) ends the session early, inside the grace.
    session = ScreenshotSession {10 * second};
    check(screenshotGoing(session, true, 10 * second + second / 10));
    check(!screenshotGoing(session, false, 10 * second + second / 5));
};
