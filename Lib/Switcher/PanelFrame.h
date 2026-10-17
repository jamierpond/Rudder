#pragma once

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <optional>
#include <string>

namespace rudder
{
// A rectangle in screen points, top-left origin (eacp's window space): the
// panel's frame, or a screen's usable area.
struct Frame
{
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;

    bool operator==(const Frame&) const = default;

    MIRO_REFLECT(x, y, width, height)
};

// The panel's default size, and the range a resize keeps it in.
constexpr float panelDefaultWidth = 640.f;
constexpr float panelDefaultHeight = 420.f;
constexpr float panelMinWidth = 480.f;
constexpr float panelMinHeight = 300.f;
constexpr float panelMaxWidth = 1600.f;
constexpr float panelMaxHeight = 1200.f;

// Which edges a press grabs, as bits: none is a move, two adjacent ones a
// corner.
namespace frameEdge
{
constexpr std::uint8_t left = 1;
constexpr std::uint8_t right = 2;
constexpr std::uint8_t top = 4;
constexpr std::uint8_t bottom = 8;
} // namespace frameEdge

// How close to an edge, in points, a press resizes rather than moves.
constexpr float frameEdgeBand = 6.f;

// The edges within `band` of (x, y), in a panel `width` by `height`. Corners
// take a little more room along each side, so they are easy to hit.
std::uint8_t frameEdgesAt(float x, float y, float width, float height, float band = frameEdgeBand);

// `start` dragged by (dx, dy) on `edges` (none: moved). The opposite edges stay
// put, and the size stays within the panel's min and max.
Frame draggedFrame(const Frame& start, std::uint8_t edges, float dx, float dy);

// The frame's size held within the panel's range and `area`, and moved the
// least that puts it wholly inside `area`.
Frame clampedFrame(const Frame& frame, const Frame& area);

// A frame of that size, centred in `area` (and clamped to it).
Frame centredFrame(float width, float height, const Frame& area);

// Where the panel opens on a screen with usable `area`: where the user left
// it (`saved`), kept inside the screen; centred instead -- at the saved size,
// or the default one -- when there is none or most of it is off the screen.
Frame placedFrame(const std::optional<Frame>& saved, const Frame& area);

// What a screen's saved frame is filed under: its usable area, so the same
// display in the same arrangement finds its frame again, and a different one
// (unplugged, rearranged) starts fresh.
std::string screenKey(const Frame& area);

// The panel's frame per screen, kept across summons and relaunches. Rare,
// deliberate writes: give it a Durable database.
struct PanelFrameStore
{
    explicit PanelFrameStore(const emberstore::Database& db);

    std::optional<Frame> savedFor(const Frame& area);
    void save(const Frame& area, const Frame& frame);

    // Back to the default: centred, at the default size.
    void forget(const Frame& area);

    emberstore::Collection<Frame> frames;
};
} // namespace rudder
