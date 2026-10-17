#include "DragHandle.h"

namespace rudder
{
Frame dragHandleBar(float width)
{
    return {(width - dragHandleWidth) / 2.f, dragHandleTop, dragHandleWidth, dragHandleHeight};
}

bool overDragHandle(float x, float y, float width)
{
    auto left = (width - dragHandleHitWidth) / 2.f;
    return x >= left && x <= left + dragHandleHitWidth && y >= 0.f
           && y <= dragHandleHitHeight;
}

bool pressMovesPanel(float x, float y, float width, float height)
{
    if (overDragHandle(x, y, width))
        return true;

    return x >= 0.f && x <= width && y >= 0.f && y < moveStripHeight
           && frameEdgesAt(x, y, width, height) == 0;
}
} // namespace rudder
