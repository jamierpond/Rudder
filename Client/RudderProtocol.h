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
//   app -> Rudder   {"type":"hello","app":"Grazer","bundleId":"audio.pond.grazer","pid":42,
//                    "canClose":true}
//   app -> Rudder   {"type":"contexts","contexts":[{"id":"3","title":"…","url":"…"}]}
//                   (each context may also carry "subtitle", "path", "branch",
//                   "open": false and "focused": true)
//   Rudder -> app   {"type":"activate","contextId":"3","raise":true}
//   Rudder -> app   {"type":"close","contextId":"3"}
//
// activate with "raise": false is a preview: the app only switches to the
// context (its tab, its pane) and must not activate itself or raise a window
// -- Rudder brings the app forward itself, and drops it again when the
// selection moves on. "raise" left out means true, so an app built before it
// existed still sees a plain activate; Enter is always a raising one.
//
// close goes only to an app whose hello said "canClose": true, and only for
// one of its open contexts: the app closes it (the tab, the pane) and pushes
// its list without it. There is no separate "closed" reply -- a context
// missing from a push is gone, however it was closed.
//
// hello comes first; each contexts push replaces the app's whole list; the
// connection closing means the app quit. Rudder announces "rudder.started"
// when it starts, so apps reconnect: a distributed notification on macOS, a
// named event it keeps set while it runs on Windows.
namespace rudder::protocol
{
struct Hello
{
    std::string app;
    std::string bundleId;
    std::int64_t pid = 0;

    // The app carries out close.
    bool canClose = false;

    MIRO_REFLECT(app, bundleId, pid, canClose)
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

    // The absolute directory the context is about, if it has one: a terminal
    // pane's cwd, a project folder. Rudder matches on it, below the title.
    std::string path;

    // The one context the app is showing now (its front tab, its focused
    // pane). Rudder switches the app back to it when Esc undoes a preview.
    bool focused = false;

    // The git branch checked out at `path`, if it is in a repo and HEAD isn't
    // detached. A context's most telling name: Rudder matches on it first.
    std::string branch;

    MIRO_REFLECT(id, title, subtitle, url, open, path, focused, branch)

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

struct Close
{
    std::string contextId;

    MIRO_REFLECT(contextId)
};

using Message = std::variant<Hello, Contexts, Activate, Close>;

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

// Where Rudder listens: the socket file
// ~/Library/Application Support/Rudder/rudder.sock on macOS, the name of an
// eacp IPC channel (a named pipe only this user can open) on Windows.
std::string endpoint();

constexpr auto startedNotification = "rudder.started";
} // namespace rudder::protocol
