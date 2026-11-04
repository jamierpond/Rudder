#include "SnapshotFrame.h"

#include <algorithm>

namespace rudder
{
namespace
{
double getPrimaryScreenHeight(const std::vector<ScreenRect>& screenFrames)
{
    if (screenFrames.empty())
        return 0.0;

    auto primary = std::find_if(screenFrames.begin(),
                                screenFrames.end(),
                                [](const ScreenRect& frame)
                                { return frame.x == 0.0 && frame.y == 0.0; });

    return primary != screenFrames.end() ? primary->height : screenFrames.front().height;
}
} // namespace

ScreenRect cocoaFrameForWindowBounds(const ScreenRect& bounds,
                                     const std::vector<ScreenRect>& screenFrames)
{
    auto primaryHeight = getPrimaryScreenHeight(screenFrames);
    return {bounds.x, primaryHeight - bounds.y - bounds.height, bounds.width, bounds.height};
}
} // namespace rudder
