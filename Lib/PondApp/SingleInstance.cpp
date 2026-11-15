#include "SingleInstance.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Time.h>
#include <eacp/Network/IPCRpc/RpcClient.h>
#include <eacp/Network/IPCRpc/RpcServer.h>

#include <Miro/Miro.h>

#include <exception>
#include <optional>
#include <utility>

#if defined(_WIN32)
#include <eacp/Core/Utils/WinInclude.h>
#else
#include <unistd.h>
#endif

namespace pondapp
{
namespace
{
constexpr auto dialTimeout = eacp::Time::MS {1000};
constexpr auto claimRetryTimeout = eacp::Time::MS {3000};
constexpr auto connectPollInterval = eacp::Time::MS {25};
constexpr auto claimPollInterval = eacp::Time::MS {100};

int getCurrentProcessId()
{
#if defined(_WIN32)
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(::getpid());
#endif
}

// Windows lets only the foreground process bring a window forward, so the
// launch the user just clicked hands that right to the holder before asking.
// macOS has no such right to hand over.
void allowProcessToComeForward(int processId)
{
#if defined(_WIN32)
    AllowSetForegroundWindow(static_cast<DWORD>(processId));
#else
    (void) processId;
#endif
}

std::string getChannelName(const std::string& instanceName)
{
    return instanceName + ".instance";
}

struct InstanceInfo
{
    int processId = 0;

    MIRO_REFLECT(processId)
};

struct SecondLaunch
{
    std::vector<std::string> arguments;

    MIRO_REFLECT(arguments)
};

struct SecondLaunchReply
{
    bool received = false;

    MIRO_REFLECT(received)
};

class InstanceApi
{
public:
    MIRO_REFLECT_API(whoIsThere, launchAgain)

    InstanceInfo whoIsThere() { return {.processId = getCurrentProcessId()}; }

    SecondLaunchReply launchAgain(const SecondLaunch& launch)
    {
        if (handler)
            handler(launch.arguments);
        else
            pendingLaunches.push_back(launch.arguments);

        return {.received = true};
    }

    void setHandler(std::function<void(const std::vector<std::string>&)> newHandler)
    {
        handler = std::move(newHandler);

        for (const auto& arguments: std::exchange(pendingLaunches, {}))
            handler(arguments);
    }

private:
    std::function<void(const std::vector<std::string>&)> handler;
    std::vector<std::vector<std::string>> pendingLaunches;
};

void waitUntilConnected(eacp::IPC::RpcClient& client)
{
    auto deadline = eacp::Time::Deadline {dialTimeout};

    while (!deadline.expired() && !client.isConnected())
        eacp::Threads::runEventLoopFor(connectPollInterval);
}
} // namespace

struct SingleInstance::Impl
{
    explicit Impl(std::string instanceNameToUse)
        : instanceName(std::move(instanceNameToUse))
    {
        bridge.use(api);
        tryClaim();
    }

    void tryClaim()
    {
        try
        {
            server.emplace(getChannelName(instanceName), bridge);
        }
        catch (const std::exception&)
        {
        }
    }

    bool tellRunningInstance(const std::vector<std::string>& arguments)
    {
        try
        {
            auto client = eacp::IPC::RpcClient {getChannelName(instanceName), dialTimeout};
            waitUntilConnected(client);

            if (!client.isConnected())
                return false;

            const auto running = client.call<InstanceInfo>("whoIsThere").waitFor(dialTimeout);
            allowProcessToComeForward(running.processId);

            const auto launch = SecondLaunch {.arguments = arguments};
            return client.call<SecondLaunchReply>("launchAgain", launch)
                .waitFor(dialTimeout)
                .received;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    void claimWhenFree()
    {
        auto deadline = eacp::Time::Deadline {claimRetryTimeout};

        while (!server && !deadline.expired())
        {
            eacp::Threads::runEventLoopFor(claimPollInterval);
            tryClaim();
        }
    }

    std::string instanceName;
    InstanceApi api;
    Miro::Bridge bridge;
    std::optional<eacp::IPC::RpcServer> server;
};

SingleInstance::SingleInstance(std::string instanceName)
    : impl(std::make_unique<Impl>(std::move(instanceName)))
{
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::isTheRunningInstance() const
{
    return impl->server.has_value();
}

bool SingleInstance::handOverToRunningInstance(const std::vector<std::string>& arguments)
{
    if (isTheRunningInstance())
        return false;

    if (impl->tellRunningInstance(arguments))
        return true;

    impl->claimWhenFree();
    return false;
}

void SingleInstance::setSecondLaunchHandler(
    std::function<void(const std::vector<std::string>&)> handler)
{
    impl->api.setHandler(std::move(handler));
}

std::vector<std::string> getLaunchArguments(int argc, char** argv)
{
    if (argc < 1)
        return {};

    return {argv + 1, argv + argc};
}
} // namespace pondapp
