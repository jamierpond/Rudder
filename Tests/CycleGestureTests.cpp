#include <CycleGesture.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
constexpr auto panelHidden = false;
constexpr auto panelShown = true;
constexpr auto chordHasModifiers = true;
constexpr auto modifiersHeld = true;
constexpr auto modifiersReleased = false;

CycleGesture openedByCycleChord()
{
    auto gesture = CycleGesture {};
    gesture.pressCycleChord(panelHidden, chordHasModifiers);
    return gesture;
}
} // namespace

auto tCycleOpensCycling = test("CycleGesture/the chord opens a hidden panel in the cycling state") = []
{
    auto gesture = CycleGesture {};

    check(gesture.pressCycleChord(panelHidden, chordHasModifiers) == CycleStep::openPanelCycling);
    check(gesture.isCycling());
};

auto tCycleBareKeyOpensLikeSummon = test("CycleGesture/a chord without modifiers opens the panel without cycling") = []
{
    auto gesture = CycleGesture {};

    check(gesture.pressCycleChord(panelHidden, !chordHasModifiers) == CycleStep::openPanel);
    check(!gesture.isCycling());
};

auto tCycleFurtherPressesMove = test("CycleGesture/further presses move down, with Shift up") = []
{
    auto gesture = openedByCycleChord();

    check(gesture.pressCycleChord(panelShown, chordHasModifiers) == CycleStep::moveSelectionDown);
    check(gesture.pressReverseCycleChord(panelShown) == CycleStep::moveSelectionUp);
    check(gesture.isCycling());
};

auto tCycleReleasePicks = test("CycleGesture/letting go of the chord's modifiers picks once") = []
{
    auto gesture = openedByCycleChord();

    check(gesture.changeHeldModifiers(modifiersHeld) == CycleStep::nothing);
    check(gesture.changeHeldModifiers(modifiersReleased) == CycleStep::pickSelection);
    check(!gesture.isCycling());
    check(gesture.changeHeldModifiers(modifiersReleased) == CycleStep::nothing);
};

auto tCycleInSummonedPanelOnlyMoves = test("CycleGesture/in a panel opened by summon the chord moves and release does nothing") = []
{
    auto gesture = CycleGesture {};

    check(gesture.pressCycleChord(panelShown, chordHasModifiers) == CycleStep::moveSelectionDown);
    check(!gesture.isCycling());
    check(gesture.changeHeldModifiers(modifiersReleased) == CycleStep::nothing);
};

auto tCycleReverseIgnoredWhileHidden = test("CycleGesture/the reverse chord does nothing while the panel is hidden") = []
{
    auto gesture = CycleGesture {};
    check(gesture.pressReverseCycleChord(panelHidden) == CycleStep::nothing);
};

auto tCycleEndStopsPick = test("CycleGesture/closing the panel ends cycling, so a later release picks nothing") = []
{
    auto gesture = openedByCycleChord();
    gesture.endCycling();

    check(!gesture.isCycling());
    check(gesture.changeHeldModifiers(modifiersReleased) == CycleStep::nothing);
};

auto tCycleWrapsBothWays = test("CycleGesture/rows wrap past either end") = []
{
    constexpr auto rowCount = 4;
    constexpr auto lastRow = rowCount - 1;

    check(calculateWrappedRow(1, 1, rowCount) == 2);
    check(calculateWrappedRow(lastRow, 1, rowCount) == 0);
    check(calculateWrappedRow(0, -1, rowCount) == lastRow);
    check(calculateWrappedRow(0, 1, 0) == 0);
};
