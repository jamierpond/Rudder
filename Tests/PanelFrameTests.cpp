#include <PanelFrame.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
// A 1512x982 laptop screen under a 33-point menu bar.
const auto laptop = Frame {0.f, 33.f, 1512.f, 949.f};
const auto start = Frame {400.f, 200.f, 640.f, 420.f};
} // namespace

auto tEdgesAt = test("PanelFrame/the edges under the pointer") = []
{
    check(frameEdgesAt(320.f, 200.f, 640.f, 420.f) == 0);
    check(frameEdgesAt(2.f, 200.f, 640.f, 420.f) == frameEdge::left);
    check(frameEdgesAt(638.f, 200.f, 640.f, 420.f) == frameEdge::right);
    check(frameEdgesAt(320.f, 1.f, 640.f, 420.f) == frameEdge::top);
    check(frameEdgesAt(320.f, 418.f, 640.f, 420.f) == frameEdge::bottom);

    // Corners reach a little along each side.
    check(frameEdgesAt(1.f, 1.f, 640.f, 420.f) == (frameEdge::left | frameEdge::top));
    check(frameEdgesAt(630.f, 418.f, 640.f, 420.f)
          == (frameEdge::right | frameEdge::bottom));
    check(frameEdgesAt(638.f, 412.f, 640.f, 420.f)
          == (frameEdge::right | frameEdge::bottom));

    // Outside the panel is nothing.
    check(frameEdgesAt(-1.f, 200.f, 640.f, 420.f) == 0);
};

auto tMove = test("PanelFrame/a move keeps the size") = []
{
    check(draggedFrame(start, 0, 30.f, -50.f) == Frame {430.f, 150.f, 640.f, 420.f});
};

auto tResize = test("PanelFrame/a resize moves only the grabbed edges") = []
{
    check(draggedFrame(start, frameEdge::right, 100.f, 999.f)
          == Frame {400.f, 200.f, 740.f, 420.f});
    check(draggedFrame(start, frameEdge::bottom, 999.f, 80.f)
          == Frame {400.f, 200.f, 640.f, 500.f});

    // Left and top keep the opposite edge where it was.
    check(draggedFrame(start, frameEdge::left, -60.f, 0.f)
          == Frame {340.f, 200.f, 700.f, 420.f});
    check(draggedFrame(start, frameEdge::left | frameEdge::top, 40.f, 20.f)
          == Frame {440.f, 220.f, 600.f, 400.f});
};

auto tResizeLimits = test("PanelFrame/a resize stops at the min and max") = []
{
    auto small = draggedFrame(start, frameEdge::left | frameEdge::top, 1000.f, 1000.f);
    check(small.width == panelMinWidth);
    check(small.height == panelMinHeight);
    // Pinned on the right and bottom edges.
    check(small.x + small.width == start.x + start.width);
    check(small.y + small.height == start.y + start.height);

    auto big = draggedFrame(start, frameEdge::right | frameEdge::bottom, 5000.f, 5000.f);
    check(big.width == panelMaxWidth);
    check(big.height == panelMaxHeight);
};

auto tClamp = test("PanelFrame/a frame is kept on its screen") = []
{
    // Hanging off the right and bottom: pushed back in, size kept.
    check(clampedFrame({1200.f, 800.f, 640.f, 420.f}, laptop)
          == Frame {872.f, 562.f, 640.f, 420.f});

    // Above the menu bar: down under it.
    check(clampedFrame({100.f, 0.f, 640.f, 420.f}, laptop).y == 33.f);

    // Bigger than the screen: no bigger than it.
    check(clampedFrame({0.f, 0.f, 1600.f, 1200.f}, laptop) == laptop);
};

auto tPlaceDefault = test("PanelFrame/nothing saved opens centred at the default size") = []
{
    auto frame = placedFrame(std::nullopt, laptop);

    check(frame.width == panelDefaultWidth);
    check(frame.height == panelDefaultHeight);
    check(frame.x == (1512.f - 640.f) / 2.f);
    check(frame.y == 33.f + (949.f - 420.f) / 2.f);
};

auto tPlaceSaved = test("PanelFrame/a saved frame opens where it was left") = []
{
    check(placedFrame(start, laptop) == start);

    // Mostly on screen: nudged in.
    check(placedFrame(Frame {1000.f, 200.f, 640.f, 420.f}, laptop).x == 872.f);
};

auto tPlaceOffScreen = test("PanelFrame/a frame mostly off the screen falls back to centred") = []
{
    // Left on a display to the right that is gone now.
    auto frame = placedFrame(Frame {1800.f, 200.f, 800.f, 500.f}, laptop);

    check(frame == centredFrame(800.f, 500.f, laptop));
    check(frame.x == (1512.f - 800.f) / 2.f);
};

auto tScreenKey = test("PanelFrame/screens are told apart by their area") = []
{
    check(screenKey(laptop) == "0,33 1512x949");
    check(screenKey(laptop) != screenKey({1512.f, 0.f, 2560.f, 1415.f}));
};

auto tFrames = test("PanelFrame/the frame per screen is saved and reset") = []
{
    auto external = Frame {1512.f, 0.f, 2560.f, 1415.f};
    auto onExternal = Frame {1700.f, 300.f, 900.f, 600.f};
    auto frames = PanelFrames {};

    check(!savedFrame(frames, laptop));
    check(!forgetFrame(frames, laptop));

    saveFrame(frames, laptop, start);
    saveFrame(frames, external, onExternal);
    check(savedFrame(frames, laptop) == start);
    check(savedFrame(frames, external) == onExternal);

    check(forgetFrame(frames, laptop));
    check(!savedFrame(frames, laptop));
    check(savedFrame(frames, external) == onExternal);

    // The last screen gone takes "panel" with it.
    check(forgetFrame(frames, external));
    check(!frames);
};
