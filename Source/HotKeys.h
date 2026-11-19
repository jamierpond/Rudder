#pragma once

#include "Keybinding.h"
#include "Slots.h"

#include <eacp/Graphics/Graphics.h>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
// A chord's modifiers as eacp's GlobalHotKey takes them.
eacp::Graphics::ModifierKeys getModifierKeysForChord(const Chord& chord);

// A slot chord Rudder couldn't register as it is: `chord` can't be parsed
// ("settings.bad-chord"), or the system or another app holds it
// ("settings.hotkey-clash"; registered anyway, as the other hotkeys are).
struct SlotHotKeyProblem
{
    std::string event;
    std::string chord;
};

// The slot chords as global hotkeys, while the panel is hidden (the panel's
// key capture hands them to Rudder as keys instead: findSlotPress).
class SlotHotKeys
{
public:
    explicit SlotHotKeys(std::function<void(const SlotPress&)> onPress);

    // Registers every slot chord in `bindings`, replacing those registered
    // before. An empty chord leaves its slot unbound.
    std::vector<SlotHotKeyProblem> registerChords(const Keybindings& bindings);
    void unregisterChords();

private:
    using HotKeys =
        std::array<std::optional<eacp::Graphics::GlobalHotKey>, slotCount>;

    void registerChordList(HotKeys& keys,
                           const std::vector<std::string>& chords,
                           SlotChordAction action,
                           std::vector<SlotHotKeyProblem>& problems);

    std::function<void(const SlotPress&)> onPress;
    HotKeys jumpKeys;
    HotKeys putKeys;
};
} // namespace rudder
