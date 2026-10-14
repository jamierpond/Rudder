#pragma once

#include <RudderProtocol.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rudder::platform
{
// Rudder's end of the contexts socket: listens at protocol::endpoint(),
// accepts provider apps and reads their newline-delimited messages. Idle, it
// costs nothing: dispatch sources on the main queue on macOS, threads parked
// in blocking pipe reads on Windows. Callbacks run on the main thread;
// `connection` names one accepted connection.
class ContextServer
{
public:
    ContextServer();
    ~ContextServer();

    ContextServer(const ContextServer&) = delete;
    ContextServer& operator=(const ContextServer&) = delete;

    // Starts listening (on macOS, after removing a stale socket file). False
    // if it can't -- on Windows, when another Rudder already is.
    bool start();

    void sendActivate(int connection, const std::string& contextId, bool raise);

    std::function<void(int connection, const protocol::Hello&)> onHello =
        [](int, auto&) {};
    std::function<void(int connection, std::vector<protocol::Context>)> onContexts =
        [](int, auto) {};
    std::function<void(int connection)> onClosed = [](int) {};

private:
    struct Native;
    std::unique_ptr<Native> native;
};
} // namespace rudder::platform
