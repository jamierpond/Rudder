#pragma once

#include "Items.h"
#include "Keybinding.h"

#include <Miro/JSON/Json.h>
#include <Miro/Reflect.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
constexpr auto slotCount = std::size_t {5};

// One of the places a chord jumps straight to, harpoon style. Empty, or
// exactly one target: a `url`, a `path`, an `app` (bundle id), or one of an
// app's panes or tabs: `app` plus `repo` and `branch`, or `app` plus its
// `directory`, remembered the way contextKey remembers a context so the slot
// outlives the pane.
struct Slot
{
    std::string title;
    std::string app;
    std::string repo;
    std::string branch;
    std::string directory;
    std::string url;
    std::string path;

    bool operator==(const Slot&) const = default;

    MIRO_REFLECT(title, app, repo, branch, directory, url, path)
};

bool isSlotEmpty(const Slot& slot);

// The "slots" list of rudder.json, strictly: at most slotCount entries, each
// an object of strings, empty or with exactly one target. Nullopt with
// `error` naming the bad slot and field.
std::optional<std::vector<Slot>> readSlots(const Miro::JSON& json,
                                           std::string& error);

Slot makeSlotForApp(const std::string& appId, const std::string& appName);

// A pane or tab of `appId`: by its repository and branch or directory, else
// by its url, else the app itself (a bare provider id means nothing later).
Slot makeSlotForContext(const std::string& appId,
                        const std::string& appName,
                        const RudderContext& context);

// Where the user is in the front app: the context its provider marks focused,
// else the app. `nameFor` is a context's rename by its contextKey.
Slot makeSlotForFrontApp(
    const std::string& appId,
    const std::string& appName,
    const std::vector<RudderContext>& contexts,
    const std::function<std::string(const std::string& key)>& nameFor);

// The slot a panel row becomes; nullopt for a row that can't be one (the web
// row, an app known only by pid).
std::optional<Slot> makeSlotForRow(const RudderItem& item);

// Puts `slot` in slot `index`, growing the list to slotCount, and empties any
// other slot holding the same place: a place is in one slot at most.
void assignSlot(std::vector<Slot>& slots, std::size_t index, const Slot& slot);

// The slot holding the place `item` is, for the panel's badge. Never a window
// row, nor a pane or tab only its app's slot covers.
std::optional<std::size_t> findSlotForRow(const std::vector<Slot>& slots,
                                          const RudderItem& item);

// The row to open for a slot, from the running apps' rows (kind "app", their
// providers' contexts attached) and the installed apps' paths by id. A pane
// that isn't open goes to its unopened place if its provider lists one, else
// to its app. Nullopt when there is nothing to open: an app neither running
// nor installed.
std::optional<RudderItem>
    resolveSlotTarget(const Slot& slot,
                      const std::vector<RudderItem>& runningApps,
                      const std::map<std::string, std::string>& installedPaths);

// Each row's badge: the key of the slot chord (from `jumpChords`, by slot)
// that opens the place the row is, "Y" for ⌃⌘Y; the slot's number when its
// chord is unbound; empty for a row in no slot.
std::vector<std::string>
    labelRowsWithSlotKeys(const std::vector<Slot>& slots,
                          const std::vector<std::string>& jumpChords,
                          const std::vector<RudderItem>& rows);

enum class SlotChordAction
{
    jump,
    put,
};

// A slot chord pressed: which slot, and whether to jump to it or put a
// place in it.
struct SlotPress
{
    SlotChordAction action = SlotChordAction::jump;
    std::size_t slot = 0;

    bool operator==(const SlotPress&) const = default;
};

// Which slot chord in `bindings` a key press is, if any.
std::optional<SlotPress> findSlotPress(const Keybindings& bindings,
                                       std::uint16_t keyCode,
                                       const HeldModifiers& held);

// What `chord` is already bound to among the slot chords, for Settings'
// "already bound to" message: "slot 2", "putting in slot 2".
std::optional<std::string> describeSlotChordOwner(const Keybindings& bindings,
                                                  const Chord& chord);

} // namespace rudder
