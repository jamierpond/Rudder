#pragma once

#include "Items.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
// What the user works on, learnt from focus rather than only from picks in
// the panel: the context each provider says is focused (by contextKey), and
// how long that has held while its app was in front. The caller stamps MRU
// with what pushed() and activated() return, and adds focusWeight of
// frecency for what settle() returns -- once per spell, and only after
// dwellMs, so flicking past a pane on the way to another doesn't count.
//
// Feed it nothing while the panel is up: previews move the providers' focus,
// and only what the panel commits to counts (that's a pick).
class FocusTracker
{
public:
    // Long enough that stepping through panes or tabs (well under a second
    // each) doesn't count, short enough that reading one does.
    static constexpr std::int64_t dwellMs = 5000;

    // A pick adds 1; a focus Rudder only watched is a quarter of one. Focus
    // spells are many times more common than picks (every ctrl+a arrow, every
    // cmd-tab back), so at a full point they'd swamp what the user asks for;
    // at a quarter, four held spells weigh a pick.
    static constexpr double focusWeight = 0.25;

    // A provider pushed `appId`'s contexts. When the focused one changed and
    // the app is in front: its key, to stamp. A new spell starts either way.
    std::optional<std::string> pushed(const std::string& appId,
                                      const std::vector<RudderContext>& contexts,
                                      bool frontmost,
                                      std::int64_t nowMs);

    // The user picked `key`, one of `appId`'s contexts, in the panel. The
    // pick already counted it, and the panel hid the push that moves the
    // app's focus there (nothing is fed while it is up), so the app's spell
    // is now on `key`, already credited: neither the activation that follows
    // nor the push that catches up adds the focus fraction on top, and
    // neither stamps or credits the pane the user just left.
    void picked(const std::string& appId, const std::string& key, std::int64_t nowMs);

    // `appId` came to the front: its focused context's key (if it has one),
    // to stamp along with the app. Starts a new spell -- except within
    // dwellMs of a pick of one of its contexts: that activation is the
    // pick's own (Rudder records it, then the OS reports it), and carries
    // the picked spell on.
    std::optional<std::string> activated(const std::string& appId, std::int64_t nowMs);

    // The front app's focused context once it has held dwellMs this spell,
    // and then not again until the next spell.
    std::optional<std::string> settle(const std::string& frontAppId, std::int64_t nowMs);

    // The app quit or its provider went away.
    void forget(const std::string& appId);

    // A context on `key` is gone: a spell on it ends, so neither activated()
    // nor settle() hands the key back.
    void forgetKey(const std::string& key);

private:
    struct Spell
    {
        std::string key;
        std::int64_t since = 0;
        bool credited = false;

        // Started by picked(); its own activations carry it on.
        bool picked = false;
    };

    std::map<std::string, Spell> spells;
};
} // namespace rudder
