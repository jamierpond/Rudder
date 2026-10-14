#include <CaptureGate.h>

#include <NanoTest/NanoTest.h>

#include <array>

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
