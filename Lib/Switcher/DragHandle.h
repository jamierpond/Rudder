#pragma once

#include "PanelFrame.h"

namespace rudder
{
// The grab handle centred on the panel's top edge: a small pill, and a
// slightly larger area around it that takes the press.
constexpr float dragHandleWidth = 36.f;
constexpr float dragHandleHeight = 4.f;
constexpr float dragHandleTop = 6.f;
constexpr float dragHandleHitWidth = 60.f;
constexpr float dragHandleHitHeight = 14.f;

// The empty strip along the top, above the search field: pressing it moves
// the panel too.
constexpr float moveStripHeight = 14.f;

// The pill, in a panel `width` points wide.
Frame dragHandleBar(float width);

// Over the handle's hit area.
bool overDragHandle(float x, float y, float width);

// Whether a press at (x, y) moves the panel: on the handle, or on the top
// strip away from the edges (those resize).
bool pressMovesPanel(float x, float y, float width, float height);
} // namespace rudder
