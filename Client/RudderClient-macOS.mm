#include "RudderClient.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace rudder
{
namespace
{
constexpr auto firstReconnectDelay = std::chrono::milliseconds {1000};
constexpr auto longestReconnectDelay = std::chrono::milliseconds {10000};
constexpr auto reconnectDelayGrowthFactor = 2;
constexpr auto readChunkBytes = std::size_t {16384};
constexpr auto sourcesSharingTheSocket = 2;

std::chrono::milliseconds calculateNextReconnectDelay(std::chrono::milliseconds delay)
{
    return std::min(delay * reconnectDelayGrowthFactor, longestReconnectDelay);
}

bool makeSocketNonBlocking(int fd)
{
    auto flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
} // namespace

// The connection state lives in a shared_ptr the dispatch blocks hold weakly,
// so a block arriving after the client is gone finds nothing to touch.
struct RudderClient::Native
{
    struct OutgoingLine
    {
        std::string bytes;
        bool isContexts = false;
    };

    struct State
    {
        RudderClient* owner = nullptr;
        protocol::Hello hello;
        std::string endpoint;
        std::optional<protocol::Contexts> latest;
        protocol::LineBuffer lines;
        int socket = -1;
        dispatch_source_t reader = nil;
        dispatch_source_t writer = nil;
        bool isWriterResumed = false;
        std::deque<OutgoingLine> outgoing;
        std::size_t bytesWrittenOfFrontLine = 0;
        std::chrono::milliseconds reconnectDelay = firstReconnectDelay;
        bool isReconnectScheduled = false;
        id startedObserver = nil;
    };

    std::shared_ptr<State> state = std::make_shared<State>();

    static bool connect(const std::shared_ptr<State>& state)
    {
        if (state->socket >= 0)
            return true;

        auto address = sockaddr_un {};
        address.sun_family = AF_UNIX;

        if (state->endpoint.size() >= sizeof(address.sun_path))
            return false;

        std::strncpy(address.sun_path, state->endpoint.c_str(), sizeof(address.sun_path) - 1);

        auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);

        if (fd < 0)
            return false;

        auto on = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));

        auto* genericAddress = reinterpret_cast<sockaddr*>(&address);

        // No Rudder listening is the normal case: ENOENT or ECONNREFUSED, at
        // once and quietly.
        if (::connect(fd, genericAddress, sizeof(address)) != 0 || !makeSocketNonBlocking(fd))
        {
            ::close(fd);
            return false;
        }

        state->socket = fd;
        state->lines = {};
        state->outgoing.clear();
        state->bytesWrittenOfFrontLine = 0;
        state->reconnectDelay = firstReconnectDelay;

        auto weak = std::weak_ptr<State> {state};
        auto descriptor = static_cast<uintptr_t>(fd);
        auto cancelsRemaining = std::make_shared<int>(sourcesSharingTheSocket);
        auto closeSocketOnceBothSourcesAreCancelled = ^{
          if (--*cancelsRemaining == 0)
              ::close(fd);
        };

        state->reader = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, descriptor, 0, dispatch_get_main_queue());
        dispatch_source_set_event_handler(state->reader, ^{
          if (auto alive = weak.lock())
              receive(alive);
        });
        dispatch_source_set_cancel_handler(state->reader, closeSocketOnceBothSourcesAreCancelled);
        dispatch_resume(state->reader);

        state->writer = dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE, descriptor, 0, dispatch_get_main_queue());
        dispatch_source_set_event_handler(state->writer, ^{
          if (auto alive = weak.lock())
              drainOutgoingBytes(alive);
        });
        dispatch_source_set_cancel_handler(state->writer, closeSocketOnceBothSourcesAreCancelled);
        state->isWriterResumed = false;

        send(state, state->hello);

        if (state->latest)
            send(state, *state->latest);

        return true;
    }

    static void connectOrScheduleReconnect(const std::shared_ptr<State>& state)
    {
        if (!connect(state))
            scheduleReconnect(state);
    }

    static void scheduleReconnect(const std::shared_ptr<State>& state)
    {
        if (state->isReconnectScheduled || state->owner == nullptr)
            return;

        state->isReconnectScheduled = true;
        auto delay = state->reconnectDelay;
        state->reconnectDelay = calculateNextReconnectDelay(delay);

        auto weak = std::weak_ptr<State> {state};
        auto delayInNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count();

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delayInNanoseconds), dispatch_get_main_queue(), ^{
          auto alive = weak.lock();

          if (alive == nullptr)
              return;

          alive->isReconnectScheduled = false;

          if (alive->socket < 0)
              connectOrScheduleReconnect(alive);
        });
    }

    static void disconnect(const std::shared_ptr<State>& state)
    {
        if (state->socket < 0)
            return;

        // A source cancelled while suspended never runs its cancel handler.
        if (!state->isWriterResumed)
            dispatch_resume(state->writer);

        dispatch_source_cancel(state->writer);
        dispatch_source_cancel(state->reader);
        state->writer = nil;
        state->reader = nil;
        state->isWriterResumed = false;
        state->outgoing.clear();
        state->bytesWrittenOfFrontLine = 0;
        state->socket = -1;
    }

    static void disconnectAndScheduleReconnect(const std::shared_ptr<State>& state)
    {
        disconnect(state);
        scheduleReconnect(state);
    }

    static void receive(const std::shared_ptr<State>& state)
    {
        char buffer[readChunkBytes];
        auto count = ::read(state->socket, buffer, sizeof(buffer));

        if (count <= 0)
        {
            if (count < 0 && (errno == EAGAIN || errno == EINTR))
                return;

            disconnectAndScheduleReconnect(state);
            return;
        }

        auto bytesRead = static_cast<std::size_t>(count);

        for (auto& line: state->lines.append({buffer, bytesRead}))
        {
            auto message = protocol::decode(line);

            if (!message)
                continue;

            if (state->owner == nullptr)
                continue;

            if (auto* activate = std::get_if<protocol::Activate>(&*message))
            {
                if (activate->raise)
                    state->owner->onActivate(activate->contextId);
                else
                    state->owner->onPreview(activate->contextId);
            }
            else if (auto* close = std::get_if<protocol::Close>(&*message))
            {
                state->owner->onClose(close->contextId);
            }
        }
    }

    static bool hasStartedGoingOut(const State& state, std::size_t index)
    {
        return index == 0 && state.bytesWrittenOfFrontLine > 0;
    }

    // Only a contexts line not yet begun can be swapped: a partly written one
    // must finish, as a torn line would corrupt the newline-delimited stream.
    static bool replaceQueuedContextsLine(State& state, std::string& line)
    {
        for (auto index = std::size_t {0}; index < state.outgoing.size(); ++index)
        {
            auto& queued = state.outgoing[index];

            if (queued.isContexts && !hasStartedGoingOut(state, index))
            {
                queued.bytes = std::move(line);
                return true;
            }
        }

        return false;
    }

    static void send(const std::shared_ptr<State>& state, const protocol::Message& message)
    {
        if (state->socket < 0)
            return;

        auto line = protocol::encode(message);
        auto isContexts = std::holds_alternative<protocol::Contexts>(message);

        if (!isContexts || !replaceQueuedContextsLine(*state, line))
            state->outgoing.push_back({std::move(line), isContexts});

        drainOutgoingBytes(state);
    }

    static bool hasPendingBytes(const State& state)
    {
        return !state.outgoing.empty();
    }

    static void drainOutgoingBytes(const std::shared_ptr<State>& state)
    {
        while (state->socket >= 0 && hasPendingBytes(*state))
        {
            auto& front = state->outgoing.front().bytes;
            auto* data = front.data() + state->bytesWrittenOfFrontLine;
            auto left = front.size() - state->bytesWrittenOfFrontLine;
            auto written = ::write(state->socket, data, left);

            if (written < 0 && errno == EINTR)
                continue;

            if (written < 0 && errno == EAGAIN)
            {
                waitForWritability(*state);
                return;
            }

            if (written <= 0)
            {
                disconnectAndScheduleReconnect(state);
                return;
            }

            state->bytesWrittenOfFrontLine += static_cast<std::size_t>(written);

            if (state->bytesWrittenOfFrontLine == front.size())
            {
                state->outgoing.pop_front();
                state->bytesWrittenOfFrontLine = 0;
            }
        }

        stopWaitingForWritability(*state);
    }

    static void waitForWritability(State& state)
    {
        if (state.isWriterResumed)
            return;

        dispatch_resume(state.writer);
        state.isWriterResumed = true;
    }

    static void stopWaitingForWritability(State& state)
    {
        if (state.socket < 0 || !state.isWriterResumed)
            return;

        dispatch_suspend(state.writer);
        state.isWriterResumed = false;
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
        state->hello.pid = getpid();

    auto weak = std::weak_ptr<Native::State> {state};
    auto* name = [NSString stringWithUTF8String:protocol::startedNotification];

    state->startedObserver = [NSDistributedNotificationCenter.defaultCenter
        addObserverForName:name
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification*) {
                  if (auto alive = weak.lock())
                  {
                      // A restarted Rudder: the old connection is dead even
                      // if its close hasn't been read yet.
                      Native::disconnect(alive);
                      Native::connectOrScheduleReconnect(alive);
                  }
                }];

    Native::connectOrScheduleReconnect(state);
}

RudderClient::~RudderClient()
{
    auto& state = native->state;
    state->owner = nullptr;

    if (state->startedObserver != nil)
        [NSDistributedNotificationCenter.defaultCenter removeObserver:state->startedObserver];

    Native::disconnect(state);
}

void RudderClient::pushContexts(std::vector<protocol::Context> contexts)
{
    auto& state = native->state;
    state->latest = protocol::Contexts {std::move(contexts)};
    Native::send(state, *state->latest);
}

bool RudderClient::isConnected() const
{
    return native->state->socket >= 0;
}
} // namespace rudder
