#include "PanelFrame.h"

#include <algorithm>
#include <cmath>

namespace rudder
{
namespace
{
float clampBetween(float value, float low, float high)
{
    return std::max(low, std::min(value, high));
}

float overlap(float start, float length, float areaStart, float areaLength)
{
    auto low = std::max(start, areaStart);
    auto high = std::min(start + length, areaStart + areaLength);
    return std::max(0.f, high - low);
}
} // namespace

std::uint8_t frameEdgesAt(float x, float y, float width, float height, float band)
{
    if (x < 0.f || y < 0.f || x > width || y > height)
        return 0;

    auto edges = std::uint8_t {0};

    if (x < band)
        edges |= frameEdge::left;
    else if (x > width - band)
        edges |= frameEdge::right;

    if (y < band)
        edges |= frameEdge::top;
    else if (y > height - band)
        edges |= frameEdge::bottom;

    // A corner reaches further along each side than the band does across.
    auto corner = band * 3.f;
    auto nearLeft = x < corner;
    auto nearRight = x > width - corner;
    auto nearTop = y < corner;
    auto nearBottom = y > height - corner;

    if ((edges & (frameEdge::top | frameEdge::bottom)) != 0)
    {
        if (nearLeft)
            edges |= frameEdge::left;
        else if (nearRight)
            edges |= frameEdge::right;
    }

    if ((edges & (frameEdge::left | frameEdge::right)) != 0)
    {
        if (nearTop)
            edges |= frameEdge::top;
        else if (nearBottom)
            edges |= frameEdge::bottom;
    }

    return edges;
}

Frame draggedFrame(const Frame& start, std::uint8_t edges, float dx, float dy)
{
    if (edges == 0)
        return {start.x + dx, start.y + dy, start.width, start.height};

    auto frame = start;

    if ((edges & frameEdge::right) != 0)
        frame.width = clampBetween(start.width + dx, panelMinWidth, panelMaxWidth);
    else if ((edges & frameEdge::left) != 0)
    {
        frame.width = clampBetween(start.width - dx, panelMinWidth, panelMaxWidth);
        frame.x = start.x + start.width - frame.width;
    }

    if ((edges & frameEdge::bottom) != 0)
        frame.height = clampBetween(start.height + dy, panelMinHeight, panelMaxHeight);
    else if ((edges & frameEdge::top) != 0)
    {
        frame.height = clampBetween(start.height - dy, panelMinHeight, panelMaxHeight);
        frame.y = start.y + start.height - frame.height;
    }

    return frame;
}

Frame clampedFrame(const Frame& frame, const Frame& area)
{
    auto result = frame;

    // The range first, then the screen: a screen smaller than the minimum
    // wins, so the panel never hangs off it.
    result.width = std::min(clampBetween(frame.width, panelMinWidth, panelMaxWidth),
                            area.width);
    result.height = std::min(clampBetween(frame.height, panelMinHeight, panelMaxHeight),
                             area.height);
    result.x = clampBetween(frame.x, area.x, area.x + area.width - result.width);
    result.y = clampBetween(frame.y, area.y, area.y + area.height - result.height);
    return result;
}

Frame centredFrame(float width, float height, const Frame& area)
{
    auto sized = clampedFrame({area.x, area.y, width, height}, area);
    sized.x = area.x + (area.width - sized.width) / 2.f;
    sized.y = area.y + (area.height - sized.height) / 2.f;
    return sized;
}

Frame placedFrame(const std::optional<Frame>& saved, const Frame& area)
{
    if (!saved || saved->width <= 0.f || saved->height <= 0.f)
        return centredFrame(panelDefaultWidth, panelDefaultHeight, area);

    auto shown = overlap(saved->x, saved->width, area.x, area.width)
                 * overlap(saved->y, saved->height, area.y, area.height);

    if (shown * 2.f < saved->width * saved->height)
        return centredFrame(saved->width, saved->height, area);

    return clampedFrame(*saved, area);
}

std::string screenKey(const Frame& area)
{
    auto number = [](float value) { return std::to_string((long) std::lround(value)); };

    return number(area.x) + "," + number(area.y) + " " + number(area.width) + "x"
           + number(area.height);
}

PanelFrameStore::PanelFrameStore(const emberstore::Database& db)
    : frames(db.collection<Frame>("panel-frames"))
{
}

std::optional<Frame> PanelFrameStore::savedFor(const Frame& area)
{
    return frames.get(screenKey(area));
}

void PanelFrameStore::save(const Frame& area, const Frame& frame)
{
    frames.doc(screenKey(area)).set(frame);
}

void PanelFrameStore::forget(const Frame& area)
{
    frames.remove(screenKey(area));
}
} // namespace rudder
