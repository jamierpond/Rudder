#pragma once

namespace rudder
{
// What the app does for one step of the cycle gesture.
enum class CycleStep
{
    nothing,
    // Open the panel as the summon hotkey does.
    openPanel,
    // Open it in the cycling state: letting go of the chord's modifiers picks.
    openPanelCycling,
    moveSelectionDown,
    moveSelectionUp,
    pickSelection,
};

// The cycle hotkey, cmd-tab style over the panel's resting list: hold the
// chord's modifiers, tap the key to walk down the rows (with Shift to walk
// up), let go to pick. Pressed while the panel is already open, it only moves
// the selection.
class CycleGesture
{
public:
    // A chord without modifiers has nothing to let go of, so it opens the
    // panel like the summon hotkey.
    CycleStep pressCycleChord(bool isPanelVisible, bool doesChordHaveModifiers);

    CycleStep pressReverseCycleChord(bool isPanelVisible);

    // Whether any modifier the chord names is held now, from a modifier key
    // going down or up, or read from the OS right after the panel opened.
    CycleStep changeHeldModifiers(bool isAnyChordModifierHeld);

    // The panel closed, whichever way.
    void endCycling();

    bool isCycling() const { return cycling; }

private:
    bool cycling = false;
};

// The row `delta` rows from `selected`, wrapping past either end of a list of
// `rowCount` rows.
int calculateWrappedRow(int selected, int delta, int rowCount);
} // namespace rudder
