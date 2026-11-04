#include <SnapshotFrame.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
const auto laptop = ScreenRect {0.0, 0.0, 1512.0, 982.0};
const auto monitorAbove = ScreenRect {-200.0, 982.0, 2560.0, 1440.0};
const auto monitorLeft = ScreenRect {-1920.0, -300.0, 1920.0, 1080.0};
} // namespace

auto tPrimaryOnly = test("SnapshotFrame/the primary screen flips y") = []
{
    check(cocoaFrameForWindowBounds({100.0, 50.0, 800.0, 600.0}, {laptop})
          == ScreenRect {100.0, 332.0, 800.0, 600.0});
};

auto tScreenAbove = test("SnapshotFrame/a window on a screen above the primary") = []
{
    // Above the primary screen, CoreGraphics y is negative.
    check(cocoaFrameForWindowBounds({0.0, -1000.0, 1000.0, 500.0}, {laptop, monitorAbove})
          == ScreenRect {0.0, 1482.0, 1000.0, 500.0});
};

auto tScreenLeft = test("SnapshotFrame/a window on a screen left of the primary") = []
{
    check(cocoaFrameForWindowBounds({-1800.0, 400.0, 600.0, 400.0}, {laptop, monitorLeft})
          == ScreenRect {-1800.0, 182.0, 600.0, 400.0});
};

auto tPrimaryListedLater = test("SnapshotFrame/the primary is the screen at the origin") = []
{
    check(cocoaFrameForWindowBounds({100.0, 50.0, 800.0, 600.0}, {monitorAbove, laptop})
          == ScreenRect {100.0, 332.0, 800.0, 600.0});
};
