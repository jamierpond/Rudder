#include "CycleGesture.h"

namespace rudder
{
CycleStep CycleGesture::pressCycleChord(bool isPanelVisible, bool doesChordHaveModifiers)
{
    if (isPanelVisible)
        return CycleStep::moveSelectionDown;

    if (!doesChordHaveModifiers)
        return CycleStep::openPanel;

    cycling = true;
    return CycleStep::openPanelCycling;
}

CycleStep CycleGesture::pressReverseCycleChord(bool isPanelVisible)
{
    if (!isPanelVisible)
        return CycleStep::nothing;

    return CycleStep::moveSelectionUp;
}

CycleStep CycleGesture::changeHeldModifiers(bool isAnyChordModifierHeld)
{
    if (!cycling || isAnyChordModifierHeld)
        return CycleStep::nothing;

    cycling = false;
    return CycleStep::pickSelection;
}

void CycleGesture::endCycling()
{
    cycling = false;
}

int calculateWrappedRow(int selected, int delta, int rowCount)
{
    if (rowCount <= 0)
        return 0;

    auto wrapped = (selected + delta) % rowCount;

    if (wrapped < 0)
        wrapped += rowCount;

    return wrapped;
}
} // namespace rudder
