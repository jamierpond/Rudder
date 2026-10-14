#pragma once

#include <RudderProtocol.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rudder::platform
{
// Rudder's end of the contexts socket: listens at protocol::socketPath(),
// accepts provider apps and reads their newline-delimited messages. Driven
// by dispatch sources on the main queue, so an idle socket costs nothing.
// Callbacks run on the main thread; `connection` names one accepted socket.
class ContextServer
{
public:
    ContextServer();
    ~ContextServer();

    ContextServer(const ContextServer&) = delete;
    ContextServer& operator=(const ContextServer&) = delete;

    // Removes a stale socket file and starts listening. False if it can't.
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
