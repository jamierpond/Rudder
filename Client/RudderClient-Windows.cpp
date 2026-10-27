#include "RudderClient.h"

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/WinInclude.h>
#include <eacp/Network/IPC/Channel.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace rudder
{
namespace
{
// Long enough for a Rudder that has just set the started event to finish
// handing out its first pipe instance.
constexpr auto dialTimeout = eacp::Time::MS {1000};
constexpr auto interruptRetry = eacp::Time::MS {5};

std::wstring startedEventName()
{
    return L"Local\\" + eacp::Strings::widen(protocol::startedNotification);
}
} // namespace

// The connection lives on a worker thread of its own: it waits for Rudder,
// dials, says hello, then sits in a blocking read until the pipe closes.
// Everything it hears reaches the app on the main thread. The state is shared
// with the deliveries it queues there, so one arriving after the client is
// gone finds `owner` cleared and touches nothing.
struct RudderClient::Native
{
    struct State
    {
        // Main thread only.
        RudderClient* owner = nullptr;

        protocol::Hello hello;
        std::string endpoint;

        // Guards the channel's existence and the latest list, so a push from
        // the main thread and the worker's greeting never interleave.
        std::mutex mutex;
        std::optional<eacp::IPC::Channel> channel;
        std::optional<protocol::Contexts> latest;

        std::atomic<bool> stop = false;
        std::atomic<bool> workerDone = false;

        // Manual-reset, and set for as long as some Rudder is listening.
        HANDLE started = nullptr;
        HANDLE wake = nullptr;
    };

    std::shared_ptr<State> state = std::make_shared<State>();
    std::thread worker;

    static void send(State& state, const protocol::Message& message)
    {
        if (!state.channel)
            return;

        try
        {
            state.channel->send(protocol::encode(message));
        }
        catch (const eacp::IPC::Error&)
        {
            // The reader hears the same news and drops the connection.
        }
    }

    static std::optional<eacp::IPC::Channel> dial(const std::string& endpoint)
    {
        try
        {
            return eacp::IPC::Channel::connect(endpoint, dialTimeout);
        }
        catch (const eacp::IPC::Error&)
        {
            return std::nullopt;
        }
    }

    // Blocks until a Rudder announces itself (or the client goes away), then
    // dials it. A started event still set by a Rudder that crashed leads
    // nowhere: it is cleared so the next real start wakes everyone again, and
    // dialed once more in case that start landed in between.
    static std::optional<eacp::IPC::Channel> waitForRudder(State& state)
    {
        HANDLE handles[] = {state.wake, state.started};

        while (!state.stop)
        {
            auto signalled = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            if (signalled != WAIT_OBJECT_0 + 1 || state.stop)
                return std::nullopt;

            if (auto channel = dial(state.endpoint))
                return channel;

            ResetEvent(state.started);

            if (auto channel = dial(state.endpoint))
                return channel;
        }

        return std::nullopt;
    }

    static void greet(State& state, eacp::IPC::Channel channel)
    {
        auto guard = std::lock_guard {state.mutex};
        state.channel.emplace(std::move(channel));
        send(state, state.hello);

        if (state.latest)
            send(state, *state.latest);
    }

    static void deliver(const std::shared_ptr<State>& state, protocol::Message message)
    {
        eacp::Threads::callAsync(
            [state, message = std::move(message)]
            {
                if (state->stop || state->owner == nullptr)
                    return;

                if (auto* activate = std::get_if<protocol::Activate>(&message))
                {
                    if (activate->raise)
                        state->owner->onActivate(activate->contextId);
                    else
                        state->owner->onPreview(activate->contextId);
                }
                else if (auto* close = std::get_if<protocol::Close>(&message))
                {
                    state->owner->onClose(close->contextId);
                }
            });
    }

    // The reader is the only one receiving, which the channel's
    // one-sender-one-receiver contract allows without the lock.
    static void readUntilGone(const std::shared_ptr<State>& state)
    {
        auto lines = protocol::LineBuffer {};
        char buffer[16384];

        try
        {
            while (!state->stop)
            {
                auto count = state->channel->receive(buffer, (int) sizeof(buffer));

                if (count == 0 || state->stop)
                    break;

                for (auto& line: lines.append({buffer, (std::size_t) count}))
                    if (auto message = protocol::decode(line))
                        deliver(state, std::move(*message));
            }
        }
        catch (const eacp::IPC::Error&)
        {
            // A broken pipe ends the connection the same way a clean close
            // does.
        }

        auto guard = std::lock_guard {state->mutex};
        state->channel.reset();
    }

    static void work(const std::shared_ptr<State>& state)
    {
        // Rudder may already be up: try it straight away, as the macOS
        // client does at construction.
        auto channel = dial(state->endpoint);

        while (!state->stop)
        {
            if (!channel)
                channel = waitForRudder(*state);

            if (!channel)
                break;

            greet(*state, std::move(*channel));
            channel.reset();
            readUntilGone(state);
        }

        state->workerDone = true;
    }
};

RudderClient::RudderClient(protocol::Hello hello, std::string endpoint)
    : native(std::make_unique<Native>())
{
    auto& state = native->state;
    state->owner = this;
    state->hello = std::move(hello);
    state->endpoint = std::move(endpoint);

    if (state->hello.pid == 0)
        state->hello.pid = (std::int64_t) GetCurrentProcessId();

    state->started = CreateEventW(nullptr, TRUE, FALSE, startedEventName().c_str());
    state->wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (state->started == nullptr || state->wake == nullptr)
        return;

    native->worker = std::thread([state] { Native::work(state); });
}

RudderClient::~RudderClient()
{
    auto& state = native->state;
    state->owner = nullptr;
    state->stop = true;

    if (state->wake != nullptr)
        SetEvent(state->wake);

    // A cancel only reaches a read already in flight, so keep knocking until
    // the worker has actually left (eacp's Messenger tears down the same way).
    while (native->worker.joinable() && !state->workerDone)
    {
        {
            auto guard = std::lock_guard {state->mutex};

            if (state->channel)
                state->channel->interrupt();
        }

        eacp::Time::sleep(interruptRetry);
    }

    if (native->worker.joinable())
        native->worker.join();

    state->channel.reset();

    for (auto handle: {state->started, state->wake})
        if (handle != nullptr)
            CloseHandle(handle);
}

void RudderClient::pushContexts(std::vector<protocol::Context> contexts)
{
    auto& state = *native->state;
    auto guard = std::lock_guard {state.mutex};
    state.latest = protocol::Contexts {std::move(contexts)};
    Native::send(state, *state.latest);
}

bool RudderClient::isConnected() const
{
    auto& state = *native->state;
    auto guard = std::lock_guard {state.mutex};
    return state.channel.has_value() && state.channel->isOpen();
}
} // namespace rudder
