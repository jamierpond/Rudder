#pragma once

#include "Keybinding.h"

#include <cstdint>
#include <optional>
#include <span>

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
    // The OS switched the tap off: it took too long, or the user's input did
    // (secure input, a password field). Never switched back on.
    tapTimedOut,
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
};

// `key` is the event's key for keyDown / keyUp; `passThrough` the chords that
// must still reach their global hotkeys (empty while a chord is recorded).
CaptureVerdict decideCapture(CaptureEvent event,
                             const CaptureState& state,
                             std::optional<CaptureKey> key,
                             std::span<const CaptureKey> passThrough);
} // namespace rudder
