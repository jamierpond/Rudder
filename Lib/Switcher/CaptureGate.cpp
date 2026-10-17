#include "CaptureGate.h"

#include <algorithm>

namespace rudder
{
std::optional<CaptureKey> captureKeyFor(const Chord& chord)
{
    auto code = keyCodeFor(chord.key);

    if (!code)
        return std::nullopt;

    auto modifiers = std::uint8_t {0};

    if (chord.control)
        modifiers |= captureModifiers::control;
    if (chord.option)
        modifiers |= captureModifiers::option;
    if (chord.shift)
        modifiers |= captureModifiers::shift;
    if (chord.command)
        modifiers |= captureModifiers::command;

    return CaptureKey {*code, modifiers};
}

bool mainStalled(const CaptureBacklog& backlog, std::uint64_t now, std::uint64_t cap)
{
    if (backlog.handled >= backlog.swallowed)
        return false;

    return now > backlog.lastProgress && now - backlog.lastProgress > cap;
}

CaptureVerdict decideCapture(CaptureEvent event,
                             const CaptureState& state,
                             std::optional<CaptureKey> key,
                             std::span<const CaptureKey> passThrough,
                             std::span<const CaptureKey> screenshot)
{
    // Re-enabling here is what turned a slow Rudder into a frozen machine:
    // the OS's safety net is honoured, always.
    if (event == CaptureEvent::tapTimedOut)
        return CaptureVerdict::passAndAbort;

    if (event == CaptureEvent::tapDisabledByUser)
        return CaptureVerdict::pass;

    if (!state.capturing || !state.panelShown)
        return CaptureVerdict::passAndStop;

    if (state.stalled)
        return CaptureVerdict::passAndAbort;

    if (event != CaptureEvent::keyDown && event != CaptureEvent::keyUp)
        return CaptureVerdict::pass;

    if (!key)
        return CaptureVerdict::pass;

    // Ahead of `screenshotting`, so a second chord restarts the grace.
    if (std::find(screenshot.begin(), screenshot.end(), *key) != screenshot.end())
        return CaptureVerdict::passAndScreenshot;

    if (state.screenshotting)
        return CaptureVerdict::pass;

    if (std::find(passThrough.begin(), passThrough.end(), *key) != passThrough.end())
        return CaptureVerdict::pass;

    return CaptureVerdict::swallow;
}

namespace
{
// NSEvent modifier flags, as the symbolic hotkeys store them.
constexpr std::uint64_t nsShift = 1 << 17;
constexpr std::uint64_t nsControl = 1 << 18;
constexpr std::uint64_t nsOption = 1 << 19;
constexpr std::uint64_t nsCommand = 1 << 20;

constexpr std::uint16_t noKey = 0xFFFF;

constexpr std::uint16_t key3 = 20;
constexpr std::uint16_t key4 = 21;
constexpr std::uint16_t key5 = 23;
constexpr std::uint16_t key6 = 22;

// Save the screen, copy it, save an area, copy it, the options toolbar, the
// Touch Bar.
constexpr SymbolicHotKey screenshotDefaults[] = {
    {28, true, key3, nsCommand | nsShift},
    {29, true, key3, nsControl | nsCommand | nsShift},
    {30, true, key4, nsCommand | nsShift},
    {31, true, key4, nsControl | nsCommand | nsShift},
    {184, true, key5, nsCommand | nsShift},
    {181, true, key6, nsCommand | nsShift},
};

CaptureKey captureKeyOf(const SymbolicHotKey& hotKey)
{
    auto modifiers = std::uint8_t {0};

    if ((hotKey.modifierFlags & nsControl) != 0)
        modifiers |= captureModifiers::control;
    if ((hotKey.modifierFlags & nsOption) != 0)
        modifiers |= captureModifiers::option;
    if ((hotKey.modifierFlags & nsShift) != 0)
        modifiers |= captureModifiers::shift;
    if ((hotKey.modifierFlags & nsCommand) != 0)
        modifiers |= captureModifiers::command;

    return {*hotKey.keyCode, modifiers};
}
} // namespace

std::vector<CaptureKey> screenshotChords(std::span<const SymbolicHotKey> userEntries)
{
    auto chords = std::vector<CaptureKey> {};

    for (auto& fallback: screenshotDefaults)
    {
        auto found = std::find_if(userEntries.begin(),
                                  userEntries.end(),
                                  [&](auto& entry) { return entry.id == fallback.id; });
        auto hotKey = found != userEntries.end() ? *found : fallback;

        if (!hotKey.keyCode)
        {
            hotKey.keyCode = fallback.keyCode;
            hotKey.modifierFlags = fallback.modifierFlags;
        }

        if (hotKey.enabled && *hotKey.keyCode != noKey)
            chords.push_back(captureKeyOf(hotKey));
    }

    return chords;
}

bool screenshotGoing(ScreenshotSession& session,
                     bool uiUp,
                     std::uint64_t now,
                     std::uint64_t grace)
{
    if (uiUp)
    {
        session.seen = true;
        return true;
    }

    if (session.seen)
        return false;

    return now < session.startedAt || now - session.startedAt < grace;
}
} // namespace rudder
