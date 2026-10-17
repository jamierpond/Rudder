#include <DragHandle.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

auto tHandleBar = test("DragHandle/the pill is centred on the top edge") = []
{
    check(dragHandleBar(640.f) == Frame {302.f, 6.f, 36.f, 4.f});
};

auto tHandleHit = test("DragHandle/the hit area is larger than the pill") = []
{
    check(overDragHandle(320.f, 8.f, 640.f));
    check(overDragHandle(291.f, 13.f, 640.f));
    check(overDragHandle(349.f, 0.f, 640.f));
    check(!overDragHandle(289.f, 8.f, 640.f));
    check(!overDragHandle(320.f, 15.f, 640.f));
};

auto tHandleMoves = test("DragHandle/the handle and the top strip move, the edges resize") = []
{
    // The handle wins over the top edge's resize band.
    check(pressMovesPanel(320.f, 2.f, 640.f, 420.f));

    // The strip below the edge band moves; the band itself resizes.
    check(pressMovesPanel(100.f, 10.f, 640.f, 420.f));
    check(!pressMovesPanel(100.f, 2.f, 640.f, 420.f));

    // Corners and side edges resize, even in the strip.
    check(!pressMovesPanel(2.f, 10.f, 640.f, 420.f));
    check(!pressMovesPanel(636.f, 10.f, 640.f, 420.f));

    // Below the strip -- the search field, the list -- is not a move.
    check(!pressMovesPanel(100.f, 30.f, 640.f, 420.f));
    check(!pressMovesPanel(320.f, 200.f, 640.f, 420.f));
};
