#include "RudderProtocol.h"

#include <cstdlib>

namespace rudder::protocol
{
std::string endpoint()
{
    auto* home = std::getenv("HOME");
    auto base = std::string {home != nullptr ? home : ""};
    return base + "/Library/Application Support/pond/Rudder/rudder.sock";
}
} // namespace rudder::protocol
