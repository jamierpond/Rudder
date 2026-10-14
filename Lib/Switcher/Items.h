#pragma once

#include <Miro/Reflect.h>

#include <string>
#include <vector>

namespace rudder
{
// A deep-linkable place inside an app: a Grazer tab, a CowTerm session. Mirrors
// what a provider pushes over the (future) context socket -- `contexts
// [{id, title, subtitle?, url?}]` -- so that layer can decode straight into
// it. `id` is the provider's own, unique within its app.
struct RudderContext
{
    std::string id;
    std::string title;
    std::string subtitle;
    std::string url;

    // "context" for a provider's; "window" for one of the app's own windows,
    // which Rudder finds itself (through accessibility) and focuses directly.
    std::string kind = "context";

    // A provider's place that isn't open yet (protocol open: false).
    bool open = true;

    MIRO_REFLECT(id, title, subtitle, url, kind, open)
};

// One row in the panel. kind "app" is a running app (id = appId = its bundle
// id where there is one). kind "context" or "window" is one of an app's
// contexts flattened into its own row while searching: `appId` names the owner and
// `contextId` is what goes back to it as `activate {contextId}`. Apps keep
// their contexts as children in `contexts`, empty until providers exist.
// Favourites from the config file are rows of kind "favourite". Installed
// apps that aren't running are rows too (running false, `path` set
// to the bundle, which Enter launches). Icons are not carried: the panel
// looks them up by appId.
struct RudderItem
{
    std::string kind = "app";
    std::string id;
    std::string title;
    std::string subtitle;
    std::string appId;
    std::string contextId;
    std::vector<RudderContext> contexts;
    bool running = true;
    std::string path;

    // Favourites (kind "favourite"): a url target, extra words to match on,
    // and the app a url opens with.
    std::string url;
    std::vector<std::string> keywords;
    std::string openWith;

    MIRO_REFLECT(kind,
                 id,
                 title,
                 subtitle,
                 appId,
                 contextId,
                 contexts,
                 running,
                 path,
                 url,
                 keywords,
                 openWith)
};
} // namespace rudder
