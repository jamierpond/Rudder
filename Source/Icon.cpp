#include "Icon.h"

#include <ResEmbed/ResEmbed.h>
#include <eacp/Core/Platform/Platform.h>

namespace rudder
{
namespace
{
eacp::Graphics::Image decodeResource(const std::string& name)
{
    auto data = ResEmbed::get(name);
    return eacp::Graphics::Image::decode(data.data(), (int) data.size());
}
} // namespace

eacp::Graphics::Image loadTrayIcon()
{
    if constexpr (eacp::Platform::isWindows())
        return decodeResource("TrayWindows.png");
    else
        return decodeResource("Tray.png");
}

eacp::Graphics::Image loadAppIcon()
{
    return decodeResource("Icon.png");
}
} // namespace rudder
