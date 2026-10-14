#include "RudderProtocol.h"

namespace rudder::protocol
{
// eacp folds this into a per-user named pipe; there is no folder to make or
// stale file to sweep, as there is for the macOS socket.
std::string endpoint()
{
    return "com.pond.rudder.contexts";
}
} // namespace rudder::protocol
