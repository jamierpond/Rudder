#include "HotKeys.h"

#include <algorithm>

namespace rudder
{
using eacp::Graphics::GlobalHotKey;
using eacp::Graphics::ModifierKeys;

ModifierKeys getModifierKeysForChord(const Chord& chord)
{
    auto modifiers = ModifierKeys {};
    modifiers.shift = chord.shift;
    modifiers.control = chord.control;
    modifiers.alt = chord.option;
    modifiers.command = chord.command;
    return modifiers;
}

SlotHotKeys::SlotHotKeys(std::function<void(const SlotPress&)> onPressToUse)
    : onPress(std::move(onPressToUse))
{
}

std::vector<SlotHotKeyProblem>
    SlotHotKeys::registerChords(const Keybindings& bindings)
{
    unregisterChords();

    auto problems = std::vector<SlotHotKeyProblem> {};
    registerChordList(
        jumpKeys, bindings.jumpToSlot, SlotChordAction::jump, problems);
    registerChordList(putKeys, bindings.putInSlot, SlotChordAction::put, problems);
    return problems;
}

void SlotHotKeys::unregisterChords()
{
    for (auto* keys: {&jumpKeys, &putKeys})
        for (auto& key: *keys)
            key.reset();
}

void SlotHotKeys::registerChordList(HotKeys& keys,
                                    const std::vector<std::string>& chords,
                                    SlotChordAction action,
                                    std::vector<SlotHotKeyProblem>& problems)
{
    auto count = std::min(keys.size(), chords.size());

    for (std::size_t slot = 0; slot < count; ++slot)
    {
        if (chords[slot].empty())
            continue;

        auto chord = parseChord(chords[slot]);

        if (!chord)
        {
            problems.push_back({"settings.bad-chord", chords[slot]});
            continue;
        }

        auto modifiers = getModifierKeysForChord(*chord);
        auto keyCode = *keyCodeFor(chord->key);

        if (!GlobalHotKey::isAvailable(modifiers, keyCode))
            problems.push_back({"settings.hotkey-clash", chords[slot]});

        keys[slot].emplace(
            modifiers, keyCode, [this, action, slot] { onPress({action, slot}); });
    }
}
} // namespace rudder
