#pragma once

#include "RudderProtocol.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rudder
{
// An app's connection to Rudder, for publishing its contexts. Connects at
// construction if Rudder is listening, and again whenever Rudder announces it
// has (re)started; with no Rudder running it stays silent and costs nothing.
// Main thread only.
class RudderClient
{
public:
    // `hello.pid` may be left 0: the client fills in this process's.
    explicit RudderClient(protocol::Hello hello);
    ~RudderClient();

    RudderClient(const RudderClient&) = delete;
    RudderClient& operator=(const RudderClient&) = delete;

    // Replaces everything Rudder shows for this app. Kept, so a reconnect
    // sends the latest list without being asked.
    void pushContexts(std::vector<protocol::Context> contexts);

    bool isConnected() const;

    // Rudder picked one of this app's contexts: bring it forward.
    std::function<void(const std::string& contextId)> onActivate = [](auto&) {};

private:
    struct Native;
    std::unique_ptr<Native> native;
};
} // namespace rudder
