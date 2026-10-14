#pragma once

#include <Miro/Reflect.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The contexts protocol between Rudder and the apps that publish "contexts"
// (Grazer tabs, CowTerm sessions): newline-delimited JSON over a local socket,
// one object per line with a "type" field.
//
//   app -> Rudder   {"type":"hello","app":"Grazer","bundleId":"audio.pond.grazer","pid":42}
//   app -> Rudder   {"type":"contexts","contexts":[{"id":"3","title":"…","url":"…"}]}
//                   (each context may also carry "subtitle" and "open": false)
//   Rudder -> app   {"type":"activate","contextId":"3","raise":true}
//
// activate with "raise": false is a preview: the app only switches to the
// context (its tab, its pane) and must not activate itself or raise a window
// -- Rudder brings the app forward itself, and drops it again when the
// selection moves on. "raise" left out means true, so an app built before it
// existed still sees a plain activate; Enter is always a raising one.
//
// hello comes first; each contexts push replaces the app's whole list; the
// connection closing means the app quit. Rudder posts the distributed
// notification "rudder.started" when it starts, so apps reconnect.
namespace rudder::protocol
{
struct Hello
{
    std::string app;
    std::string bundleId;
    std::int64_t pid = 0;

    MIRO_REFLECT(app, bundleId, pid)
};

struct Context
{
    std::string id;
    std::string title;
    std::string subtitle;
    std::string url;

    // False for a place the app can open on activate (a project folder that
    // becomes a new session) rather than one already open. Rudder only ever
    // activates those on Enter, never to preview them.
    bool open = true;

    MIRO_REFLECT(id, title, subtitle, url, open)

    bool operator==(const Context&) const = default;
};

struct Contexts
{
    std::vector<Context> contexts;

    MIRO_REFLECT(contexts)
};

struct Activate
{
    std::string contextId;

    // False for a preview: switch to the context, nothing more.
    bool raise = true;

    MIRO_REFLECT(contextId, raise)
};

using Message = std::variant<Hello, Contexts, Activate>;

// One line, newline included.
std::string encode(const Message& message);

// Nullopt for anything that isn't a well-formed message of a known type --
// the socket is local, but a bad line must never take either side down.
std::optional<Message> decode(std::string_view line);

// A line longer than this is garbage: dropped, along with the rest of it.
constexpr std::size_t maxLineBytes = 1 << 20;

// Splits a byte stream into lines as it arrives in arbitrary chunks.
class LineBuffer
{
public:
    // The complete lines `bytes` finished, without their newlines.
    std::vector<std::string> append(std::string_view bytes);

private:
    std::string pending;
    bool discarding = false;
};

// ~/Library/Application Support/Rudder/rudder.sock on macOS.
std::string socketPath();

constexpr auto startedNotification = "rudder.started";
} // namespace rudder::protocol
