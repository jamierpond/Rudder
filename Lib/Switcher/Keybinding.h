#pragma once

#include <Miro/Reflect.h>
#include <eacp/Core/Platform/Platform.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
// A global hotkey: modifiers plus one key, by its canonical name ("space",
// "`", "k", "f5"). Serialised as "control+option+shift+command+key" with only
// the held modifiers, which is what the store and the settings UI exchange.
struct Chord
{
    bool control = false;
    bool option = false;
    bool shift = false;
    bool command = false;
    std::string key;

    bool operator==(const Chord&) const = default;
};

// Case-insensitive; modifiers in any order, with the usual aliases (alt/opt,
// cmd/win, ctrl); "command" is the Windows key on Windows. Nullopt for an
// unknown key, or a bare key that would swallow ordinary typing system-wide --
// only function keys may go unmodified.
std::optional<Chord> parseChord(const std::string& text);

std::string serialiseChord(const Chord& chord);

// The menu-style label: "⌥Space", "⌃⇧K" -- "Alt+Space", "Ctrl+Shift+K" on
// Windows.
std::string displayChord(const Chord& chord);

// The macOS virtual key code for a canonical key name -- the space
// eacp::Graphics::KeyCode and GlobalHotKey use on every platform.
std::optional<std::uint16_t> keyCodeFor(const std::string& key);

// The reverse, for recording a chord from a key event: the canonical name of
// a virtual key code, or nullopt for a key a chord can't use.
std::optional<std::string> keyNameFor(std::uint16_t keyCode);

// Whether a key press is `chord`: the same key and exactly its modifiers.
bool matchesChord(const Chord& chord,
                  std::uint16_t keyCode,
                  bool control,
                  bool option,
                  bool shift,
                  bool command);

// The modifier keys down at one moment.
struct HeldModifiers
{
    bool control = false;
    bool option = false;
    bool shift = false;
    bool command = false;
};

// Whether any modifier the chord names is held: the cycle gesture picks once
// none is.
bool isAnyChordModifierHeld(const Chord& chord, const HeldModifiers& held);

// The cycle chord's reverse, which walks up: the chord plus Shift. Nullopt for
// a chord that already holds Shift.
std::optional<Chord> getReverseCycleChord(const Chord& chord);

// The slot chords' defaults, one per slot: ⌃⌘Y ⌃⌘U ⌃⌘I ⌃⌘O ⌃⌘P to jump, the
// same with Shift to put a place in the slot. Windows keeps Ctrl+Win+O (the
// On-Screen Keyboard) and something holds Ctrl+Win+P, so there they are
// Ctrl+Alt+Y to Ctrl+Alt+P.
std::vector<std::string> makeDefaultSlotChords(bool withShift);

// The stored bindings, one document in the settings collection. summon, back,
// cycle and the slot chords are global hotkeys; quit, favourite and rename only
// act inside the open panel.
// Win+D is the shell's show-desktop, which never reaches the panel, so
// Windows favourites with Ctrl+D.
// jumpToSlot and putInSlot hold one chord per slot, in slot order.
struct Keybindings
{
    std::string summon = "option+space";
    std::string back = "option+`";
    std::string quit = "control+q";
    std::string favourite = eacp::Platform::isWindows() ? "control+d" : "command+d";
    std::string rename = "control+r";
    std::string cycle = "control+tab";
    std::vector<std::string> jumpToSlot = makeDefaultSlotChords(false);
    std::vector<std::string> putInSlot = makeDefaultSlotChords(true);

    MIRO_REFLECT(summon, back, quit, favourite, rename, cycle, jumpToSlot, putInSlot)
};

// A stored document from before the slot chords loads them as empty lists:
// every slot past the end of a list gets its default chord. An empty string
// stays, and leaves that slot unbound.
void addMissingSlotChords(Keybindings& bindings);
} // namespace rudder
