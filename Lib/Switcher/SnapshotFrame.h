#pragma once

#include <vector>

namespace rudder
{
// A rectangle in global screen points. Which corner (x, y) names depends on
// the space: CoreGraphics window bounds are top-left of the primary screen,
// Cocoa frames bottom-left.
struct ScreenRect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    bool operator==(const ScreenRect&) const = default;
};

// Where a window's snapshot goes in Cocoa: `bounds` is its kCGWindowBounds
// (top-left origin, y down, measured from the primary screen's top), and
// `screenFrames` are every screen's Cocoa frame. The primary screen is the one
// at the Cocoa origin; with none there, the first listed.
ScreenRect cocoaFrameForWindowBounds(const ScreenRect& bounds,
                                     const std::vector<ScreenRect>& screenFrames);
} // namespace rudder
