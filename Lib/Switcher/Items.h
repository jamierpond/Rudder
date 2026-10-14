#pragma once

#include <Miro/Reflect.h>

#include <string>
#include <vector>

namespace rudder
{
// A deep-linkable place inside an app: a Wim tab, a CowTerm session. Mirrors
// what a provider pushes over the (future) context socket -- `contexts
// [{id, title, subtitle?, url?}]` -- so that layer can decode straight into
// it. `id` is the provider's own, unique within its app.
struct RudderContext
{
    std::string id;
    std::string title;
    std::string subtitle;
    std::string url;

    MIRO_REFLECT(id, title, subtitle, url)
};

// One row in the panel. kind "app" is a running app (id = appId = its bundle
// id where there is one). kind "context" is one of an app's contexts
// flattened into its own row while searching: `appId` names the owner and
// `contextId` is what goes back to it as `activate {contextId}`. Apps keep
// their contexts as children in `contexts`, empty until providers exist.
// Icons are not carried: the panel looks them up by appId.
struct RudderItem
{
    std::string kind = "app";
    std::string id;
    std::string title;
    std::string subtitle;
    std::string appId;
    std::string contextId;
    std::vector<RudderContext> contexts;

    MIRO_REFLECT(kind, id, title, subtitle, appId, contextId, contexts)
};
} // namespace rudder
