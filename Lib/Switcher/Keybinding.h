#pragma once

#include <Miro/Reflect.h>

#include <cstdint>
#include <optional>
#include <string>

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
// cmd, ctrl). Nullopt for an unknown key, or a bare key that would swallow
// ordinary typing system-wide -- only function keys may go unmodified.
std::optional<Chord> parseChord(const std::string& text);

std::string serialiseChord(const Chord& chord);

// The menu-style label: "⌥Space", "⌃⇧K".
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

// The stored bindings, one document in the settings collection. summon and
// back are global hotkeys; quit only acts inside the open panel.
struct Keybindings
{
    std::string summon = "option+space";
    std::string back = "option+`";
    std::string quit = "control+q";

    MIRO_REFLECT(summon, back, quit)
};
} // namespace rudder
