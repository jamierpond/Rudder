#pragma once

#include <eacp/Graphics/Graphics.h>

namespace rudder
{
// Decoded from the PNGs in Assets/Icon, embedded at build time (see
// Scripts/make-icons.sh for how they're made).

// The menu-bar / tray glyph: on macOS an alpha-only template the system tints,
// on Windows a small tile that reads on light and dark taskbars.
eacp::Graphics::Image loadTrayIcon();

// Rudder's app icon, 1024 px. What the unlock overlay shows as the thing to
// drag.
eacp::Graphics::Image loadAppIcon();
} // namespace rudder
