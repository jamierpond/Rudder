#pragma once

#include "Keybinding.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rudder
{
// What the key capture tap does with one event. Decided on the tap's own
// thread from a handful of flags and counters, in O(1), never waiting on the
// main thread: every key and click in the login session goes through the tap
// while it is enabled, so anything slower freezes the whole machine's input.
enum class CaptureVerdict
{
    // Let the event go on to its app.
    pass,
    // Rudder takes the key; the main thread gets a copy to type with.
    swallow,
    // A system screenshot chord: let it go, and let every key after it go
    // too until the screenshot is over (the space, escape and arrows that
    // drive ⌘⇧4 and ⌘⇧5 belong to macOS, not the panel).
    passAndScreenshot,
    // Let it go and switch the tap off: capture is over (the panel hid).
    passAndStop,
    // Let it go, switch the tap off for good and tell the main thread to drop
    // the panel: something is wrong and the user must get their input back.
    passAndAbort,
};

enum class CaptureEvent
{
    keyDown,
    keyUp,
    // The OS switched the tap off because it took too long. Never switched
    // back on.
    tapTimedOut,
    // The notice macOS also sends when Rudder itself switches the tap off
    // (every stop, and the reset at each start). It arrives late, after the
    // next start, so it says nothing about the capture now on: ignored, and
    // never a reason to switch the tap back on.
    tapDisabledByUser,
    other,
};

// A key and exactly the modifiers held with it, as bits.
namespace captureModifiers
{
constexpr std::uint8_t control = 1;
constexpr std::uint8_t option = 2;
constexpr std::uint8_t shift = 4;
constexpr std::uint8_t command = 8;
} // namespace captureModifiers

struct CaptureKey
{
    std::uint16_t keyCode = 0;
    std::uint8_t modifiers = 0;

    bool operator==(const CaptureKey&) const = default;
};

// A hotkey as the tap matches it; nullopt for a chord on an unknown key.
std::optional<CaptureKey> captureKeyFor(const Chord& chord);

// Keys swallowed but not yet handled by the main thread, and when it last made
// progress (handled one, or was handed the first of a fresh batch). Uptime
// nanoseconds.
struct CaptureBacklog
{
    std::uint64_t swallowed = 0;
    std::uint64_t handled = 0;
    std::uint64_t lastProgress = 0;
};

// The hard cap: a key the main thread hasn't got to after this long means it
// is stuck, and capture gives the keyboard back.
constexpr std::uint64_t captureStallNanos = 2'000'000'000;

// Whether the main thread has sat on a swallowed key for longer than `cap`.
// No backlog is never a stall, however long the panel has been idle.
bool mainStalled(const CaptureBacklog& backlog,
                 std::uint64_t now,
                 std::uint64_t cap = captureStallNanos);

struct CaptureState
{
    // Capture is on: set by start, cleared by stop or an abort.
    bool capturing = false;
    // The panel is on screen, as the main thread last saw it.
    bool panelShown = false;
    // The main thread is stuck (mainStalled).
    bool stalled = false;
    // A screenshot the user started is still going (ScreenshotSession).
    bool screenshotting = false;
};

// `key` is the event's key for keyDown / keyUp; `passThrough` the chords that
// must still reach their global hotkeys (empty while a chord is recorded);
// `screenshot` the system's screenshot chords (screenshotChords), which always
// reach the system, recording or not.
CaptureVerdict decideCapture(CaptureEvent event,
                             const CaptureState& state,
                             std::optional<CaptureKey> key,
                             std::span<const CaptureKey> passThrough,
                             std::span<const CaptureKey> screenshot = {});

// --- Screenshots -------------------------------------------------------------

// One of macOS's screenshot shortcuts as the user's symbolic hotkeys store it
// (com.apple.symbolichotkeys, AppleSymbolicHotKeys): its id, whether it is on,
// and its virtual key code and NSEvent modifier flags (no keyCode: the entry
// carries none, and the default key holds; 0xFFFF: no key at all).
struct SymbolicHotKey
{
    int id = 0;
    bool enabled = true;
    std::optional<std::uint16_t> keyCode;
    std::uint64_t modifierFlags = 0;
};

// The screenshot chords in force: macOS's defaults (⌘⇧3, ⌃⌘⇧3, ⌘⇧4, ⌃⌘⇧4,
// ⌘⇧5, ⌘⇧6), each replaced by the user's own entry for its id when there is
// one, and dropped when that entry is switched off.
std::vector<CaptureKey> screenshotChords(std::span<const SymbolicHotKey> userEntries);

// How long after a screenshot chord its UI has to show up before capture
// takes the keyboard back.
constexpr std::uint64_t screenshotGraceNanos = 1'000'000'000;

// One screenshot, from its chord until the system's screenshot UI has come and
// gone. The main thread polls it; the tap only reads whether it is still on.
struct ScreenshotSession
{
    std::uint64_t startedAt = 0;
    // The screenshot UI has been seen up since the chord.
    bool seen = false;
};

// Whether the screenshot is still going, given whether the system's screenshot
// UI is up now. On through the grace while the UI hasn't shown yet; once it
// has, on exactly as long as it stays up.
bool screenshotGoing(ScreenshotSession& session,
                     bool uiUp,
                     std::uint64_t now,
                     std::uint64_t grace = screenshotGraceNanos);
} // namespace rudder
