#include "Icon.h"

#include <algorithm>
#include <cmath>

namespace rudder
{
namespace
{
// Coverage of the helm at (x, y) in a size x size square: a ring, a hub and
// four spokes running out past the ring like a ship's wheel's handles.
float helmCoverage(float x, float y, float size)
{
    auto scale = size / 36.f;
    auto center = (size - 1.f) / 2.f;
    auto dx = (x - center) / scale;
    auto dy = (y - center) / scale;
    auto distance = std::sqrt(dx * dx + dy * dy);

    auto ring = std::clamp(2.2f - std::abs(distance - 12.f), 0.f, 1.f);
    auto hub = std::clamp(4.5f - distance, 0.f, 1.f);
    auto spoke = 0.f;

    if (distance > 4.f && distance < 17.f)
        spoke = std::clamp(1.4f - std::min(std::abs(dx), std::abs(dy)), 0.f, 1.f);

    return std::max({ring, hub, spoke});
}
} // namespace

eacp::Graphics::Image makeTrayIcon()
{
    constexpr int size = 36;
    auto image = eacp::Graphics::Image(size, size);

    for (auto y = 0; y < size; ++y)
        for (auto x = 0; x < size; ++x)
            if (auto coverage = helmCoverage((float) x, (float) y, (float) size);
                coverage > 0.f)
                image.set(x, y, eacp::Graphics::Color(0.f, 0.f, 0.f, coverage));

    return image;
}

eacp::Graphics::Image makeAppIcon(int size)
{
    auto image = eacp::Graphics::Image(size, size);
    auto fsize = (float) size;
    auto radius = fsize * 0.22f;
    auto inset = fsize * 0.06f;

    for (auto y = 0; y < size; ++y)
    {
        for (auto x = 0; x < size; ++x)
        {
            // A rounded-square tile, antialiased at its edge.
            auto px = std::clamp((float) x + 0.5f, inset + radius, fsize - inset - radius);
            auto py = std::clamp((float) y + 0.5f, inset + radius, fsize - inset - radius);
            auto edge = std::hypot((float) x + 0.5f - px, (float) y + 0.5f - py);
            auto tile = std::clamp(radius - edge + 0.5f, 0.f, 1.f);

            if (tile <= 0.f)
                continue;

            auto shade = (float) y / fsize;
            auto blue = eacp::Graphics::Color(
                0.20f + 0.10f * (1.f - shade), 0.42f, 0.95f - 0.15f * shade, tile);

            // The glyph at 70% of the tile, centred.
            auto glyphSize = fsize * 0.7f;
            auto offset = (fsize - glyphSize) / 2.f;
            auto helm = helmCoverage((float) x - offset, (float) y - offset, glyphSize);

            image.set(x,
                      y,
                      eacp::Graphics::Color(blue.r + (1.f - blue.r) * helm,
                                            blue.g + (1.f - blue.g) * helm,
                                            blue.b + (1.f - blue.b) * helm,
                                            tile));
        }
    }

    return image;
}
} // namespace rudder
