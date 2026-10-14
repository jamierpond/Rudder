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
                             std::span<const CaptureKey> passThrough)
{
    // Re-enabling here is what turned a slow Rudder into a frozen machine:
    // the OS's safety net is honoured, always.
    if (event == CaptureEvent::tapTimedOut || event == CaptureEvent::tapDisabledByUser)
        return CaptureVerdict::passAndAbort;

    if (!state.capturing || !state.panelShown)
        return CaptureVerdict::passAndStop;

    if (state.stalled)
        return CaptureVerdict::passAndAbort;

    if (event != CaptureEvent::keyDown && event != CaptureEvent::keyUp)
        return CaptureVerdict::pass;

    if (!key)
        return CaptureVerdict::pass;

    if (std::find(passThrough.begin(), passThrough.end(), *key) != passThrough.end())
        return CaptureVerdict::pass;

    return CaptureVerdict::swallow;
}
} // namespace rudder
