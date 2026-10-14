#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
// Where you were before: the activation history, collapsed so each app
// appears once, most recent first. back() walks it like a browser's back
// button -- press it again and it keeps going further back, instead of
// cmd-tab's two-app ping-pong.
//
// While a walk is under way the activations it causes are expected and leave
// the order alone. Any other activation ends the walk: the app it landed on
// is committed as the most recent, then the new activation goes on top.
class BackStack
{
public:
    // An app became frontmost.
    void record(const std::string& appId);

    // The next app further back, or nullopt at the end of the history. Moves
    // the walk there; the caller activates it.
    std::optional<std::string> back();

    // The app quit: drop it, keeping a walk in progress pointed sensibly.
    void forget(const std::string& appId);

    bool walking() const { return cursor > 0; }

    // Most recent first; entries()[0] is where the last real activation went.
    const std::vector<std::string>& entries() const { return stack; }

    static constexpr std::size_t maxEntries = 64;

private:
    void commitWalk();

    std::vector<std::string> stack;
    std::size_t cursor = 0;
};
} // namespace rudder
