#pragma once

#include <eacp/Graphics/Graphics.h>

namespace rudder
{
// Drawn in code so the app needs no asset files.

// The menu-bar glyph: alpha only, tinted by the system as a template.
eacp::Graphics::Image makeTrayIcon();

// Rudder's app icon at `size` pixels: the glyph in white on a blue tile. What
// the unlock overlay shows as the thing to drag.
eacp::Graphics::Image makeAppIcon(int size);
} // namespace rudder
