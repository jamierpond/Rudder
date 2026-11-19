#include "Slots.h"

#include "Favourites.h"
#include "Ranking.h"

#include <algorithm>
#include <utility>

namespace rudder
{
namespace
{
bool hasRepositoryAndBranch(const std::string& repo, const std::string& branch)
{
    return !repo.empty() && !branch.empty();
}

bool isContextSlot(const Slot& slot)
{
    return hasRepositoryAndBranch(slot.repo, slot.branch) || !slot.directory.empty();
}

// A context named by more than its provider's id: what contextKey keeps
// across shells and restarts.
bool hasStableName(const RudderContext& context)
{
    return hasRepositoryAndBranch(context.repo, context.branch)
           || !context.path.empty();
}

bool isAppSlot(const Slot& slot)
{
    return slot.url.empty() && slot.path.empty() && !slot.app.empty()
           && !isContextSlot(slot);
}

RudderContext makeContextForSlot(const Slot& slot)
{
    auto context = RudderContext {};
    context.repo = slot.repo;
    context.branch = slot.branch;
    context.path = slot.directory;
    return context;
}

// What makes two slots the same place, so it is in one slot at most.
std::string calculateSlotIdentity(const Slot& slot)
{
    if (!slot.url.empty())
        return "url:" + normaliseUrl(slot.url);

    if (!slot.path.empty())
        return "path:" + slot.path;

    if (isContextSlot(slot))
        return "context:" + contextKey(slot.app, makeContextForSlot(slot));

    return "app:" + slot.app;
}

bool isSlotHoldingPlace(const Slot& slot, const std::string& identity)
{
    return !isSlotEmpty(slot) && calculateSlotIdentity(slot) == identity;
}

std::string describeSlot(std::size_t index)
{
    return "slot " + std::to_string(index + 1);
}

std::string checkSlotTargets(const Slot& slot, std::size_t index)
{
    auto targets =
        (int) !slot.url.empty() + (int) !slot.path.empty() + (int) !slot.app.empty();
    auto namesContext =
        !slot.repo.empty() || !slot.branch.empty() || !slot.directory.empty();

    if (targets != 1)
        return describeSlot(index)
               + R"( needs exactly one of "url", "app" or "path")";

    if (namesContext && slot.app.empty())
        return describeSlot(index)
               + R"(: "repo", "branch" and "directory" need an "app")";

    if (namesContext && !isContextSlot(slot))
        return describeSlot(index)
               + R"( needs "repo" with "branch", or "directory")";

    return {};
}

std::optional<Slot>
    readSlot(const Miro::JSON& json, std::size_t index, std::string& error)
{
    if (!json.isObject())
    {
        error = describeSlot(index) + " must be an object";
        return std::nullopt;
    }

    auto slot = Slot {};

    for (auto [field, out]: {std::pair {"title", &slot.title},
                             std::pair {"app", &slot.app},
                             std::pair {"repo", &slot.repo},
                             std::pair {"branch", &slot.branch},
                             std::pair {"directory", &slot.directory},
                             std::pair {"url", &slot.url},
                             std::pair {"path", &slot.path}})
    {
        auto* value = Miro::Json::find(json.asObject(), field);

        if (value == nullptr)
            continue;

        if (!value->isString())
        {
            error = describeSlot(index) + ": \"" + field + "\" must be a string";
            return std::nullopt;
        }

        *out = value->asString();
    }

    if (isSlotEmpty(slot))
        return slot;

    error = checkSlotTargets(slot, index);

    if (!error.empty())
        return std::nullopt;

    return slot;
}

std::optional<RudderItem>
    findRunningAppRow(const std::vector<RudderItem>& runningApps,
                      const std::string& appId)
{
    auto isTheApp = [&](const RudderItem& app)
    { return app.kind == "app" && app.appId == appId; };
    auto found = std::find_if(runningApps.begin(), runningApps.end(), isTheApp);

    if (found == runningApps.end())
        return std::nullopt;

    return *found;
}

// The pane the slot remembers, open if one is, else its unopened place.
std::optional<RudderItem> findContextRow(const Slot& slot,
                                         const std::vector<RudderItem>& runningApps)
{
    auto app = findRunningAppRow(runningApps, slot.app);

    if (!app)
        return std::nullopt;

    auto wanted = contextKey(slot.app, makeContextForSlot(slot));
    auto isWanted = [&](const RudderContext& context)
    {
        return context.kind == "context"
               && contextKey(app->appId, context) == wanted;
    };
    auto isWantedAndOpen = [&](const RudderContext& context)
    { return isWanted(context) && context.open; };

    auto& contexts = app->contexts;
    auto open = std::find_if(contexts.begin(), contexts.end(), isWantedAndOpen);

    if (open != contexts.end())
        return makeContextRow(*app, *open);

    auto place = std::find_if(contexts.begin(), contexts.end(), isWanted);

    if (place != contexts.end())
        return makeContextRow(*app, *place);

    return std::nullopt;
}

std::optional<RudderItem>
    resolveAppTarget(const Slot& slot,
                     const std::vector<RudderItem>& runningApps,
                     const std::map<std::string, std::string>& installedPaths)
{
    auto row = RudderItem {};
    row.id = row.appId = slot.app;
    row.title = slot.title;

    if (auto app = findRunningAppRow(runningApps, slot.app))
    {
        row.title = app->title;
        return row;
    }

    auto installed = installedPaths.find(slot.app);

    if (installed == installedPaths.end())
        return std::nullopt;

    row.running = false;
    row.path = installed->second;
    return row;
}

// A url or path opens the way a favourite of it does.
RudderItem makeFavouriteRowForSlot(const Slot& slot)
{
    auto row = RudderItem {};
    row.kind = "favourite";
    row.id = "slot:" + calculateSlotIdentity(slot);
    row.title = slot.title;
    row.url = slot.url;
    row.path = slot.path;
    return row;
}

Slot makeSlotForFavouriteRow(const RudderItem& item)
{
    auto slot = Slot {};
    slot.title = item.title;

    if (!item.url.empty())
        slot.url = item.url;
    else if (!item.path.empty())
        slot.path = item.path;
    else
        slot.app = item.appId;

    return slot;
}

std::string getSlotKeyLabel(const std::vector<std::string>& jumpChords,
                            std::size_t index)
{
    auto chord =
        index < jumpChords.size() ? parseChord(jumpChords[index]) : std::nullopt;

    if (!chord)
        return std::to_string(index + 1);

    auto keyAlone = Chord {};
    keyAlone.key = chord->key;
    return displayChord(keyAlone);
}

RudderContext makeContextForRow(const RudderItem& item)
{
    auto context = RudderContext {};
    context.id = item.contextId;
    context.title = item.title;
    context.url = item.url;
    context.repo = item.repo;
    context.branch = item.branch;
    context.path = item.contextPath;
    return context;
}
std::optional<std::size_t>
    findSlotForKeyPress(const std::vector<std::string>& chords,
                        std::uint16_t keyCode,
                        const HeldModifiers& held)
{
    for (std::size_t index = 0; index < chords.size(); ++index)
    {
        auto chord = parseChord(chords[index]);

        if (chord
            && matchesChord(*chord,
                            keyCode,
                            held.control,
                            held.option,
                            held.shift,
                            held.command))
        {
            return index;
        }
    }

    return std::nullopt;
}

std::optional<std::size_t> findSlotForChord(const std::vector<std::string>& chords,
                                            const Chord& chord)
{
    for (std::size_t index = 0; index < chords.size(); ++index)
        if (parseChord(chords[index]) == chord)
            return index;

    return std::nullopt;
}
} // namespace

bool isSlotEmpty(const Slot& slot)
{
    return slot.app.empty() && slot.url.empty() && slot.path.empty()
           && slot.repo.empty() && slot.branch.empty() && slot.directory.empty()
           && slot.title.empty();
}

std::optional<std::vector<Slot>> readSlots(const Miro::JSON& json,
                                           std::string& error)
{
    if (!json.isArray())
    {
        error = R"("slots" must be a list)";
        return std::nullopt;
    }

    auto& entries = json.asArray();

    if (entries.size() > slotCount)
    {
        error = R"("slots" holds at most )" + std::to_string(slotCount) + " entries";
        return std::nullopt;
    }

    auto slots = std::vector<Slot> {};

    for (auto& entry: entries)
    {
        auto slot = readSlot(entry, slots.size(), error);

        if (!slot)
            return std::nullopt;

        slots.push_back(std::move(*slot));
    }

    return slots;
}

Slot makeSlotForApp(const std::string& appId, const std::string& appName)
{
    auto slot = Slot {};
    slot.title = appName;
    slot.app = appId;
    return slot;
}

Slot makeSlotForContext(const std::string& appId,
                        const std::string& appName,
                        const RudderContext& context)
{
    auto slot = Slot {};
    slot.title = context.name.empty() ? context.title : context.name;

    if (hasStableName(context))
    {
        slot.app = appId;
        slot.repo = context.repo;
        slot.branch = context.branch;
        slot.directory = context.path;
        return slot;
    }

    if (!context.url.empty())
    {
        slot.url = context.url;
        return slot;
    }

    return makeSlotForApp(appId, appName);
}

Slot makeSlotForFrontApp(
    const std::string& appId,
    const std::string& appName,
    const std::vector<RudderContext>& contexts,
    const std::function<std::string(const std::string& key)>& nameFor)
{
    auto focused =
        std::find_if(contexts.begin(),
                     contexts.end(),
                     [](const RudderContext& context) { return context.focused; });

    if (focused == contexts.end())
        return makeSlotForApp(appId, appName);

    auto context = *focused;
    context.name = nameFor(contextKey(appId, context));
    return makeSlotForContext(appId, appName, context);
}

std::optional<Slot> makeSlotForRow(const RudderItem& item)
{
    if (item.kind == "web")
        return std::nullopt;

    if (item.kind == "favourite")
        return makeSlotForFavouriteRow(item);

    if (item.appId.empty() || item.appId.starts_with("pid:"))
        return std::nullopt;

    if (item.kind == "context")
        return makeSlotForContext(
            item.appId, item.subtitle, makeContextForRow(item));

    if (item.kind == "window")
        return makeSlotForApp(item.appId, item.subtitle);

    return makeSlotForApp(item.appId, item.title);
}

void assignSlot(std::vector<Slot>& slots, std::size_t index, const Slot& slot)
{
    if (index >= slotCount)
        return;

    if (slots.size() < slotCount)
        slots.resize(slotCount);

    auto identity = calculateSlotIdentity(slot);

    for (std::size_t other = 0; other < slots.size(); ++other)
    {
        if (other != index && isSlotHoldingPlace(slots[other], identity))
            slots[other] = {};
    }

    slots[index] = slot;
}

std::optional<std::size_t> findSlotForRow(const std::vector<Slot>& slots,
                                          const RudderItem& item)
{
    if (item.kind == "window")
        return std::nullopt;

    auto slot = makeSlotForRow(item);

    if (!slot)
        return std::nullopt;

    // A pane with only its provider's id stands for its app: not the app's slot.
    if (item.kind == "context" && isAppSlot(*slot))
        return std::nullopt;

    auto identity = calculateSlotIdentity(*slot);

    for (std::size_t index = 0; index < slots.size(); ++index)
        if (isSlotHoldingPlace(slots[index], identity))
            return index;

    return std::nullopt;
}

std::optional<RudderItem>
    resolveSlotTarget(const Slot& slot,
                      const std::vector<RudderItem>& runningApps,
                      const std::map<std::string, std::string>& installedPaths)
{
    if (!slot.url.empty())
    {
        if (auto open = contextForUrl(runningApps, slot.url))
            return open;

        return makeFavouriteRowForSlot(slot);
    }

    if (!slot.path.empty())
        return makeFavouriteRowForSlot(slot);

    if (!isContextSlot(slot))
        return resolveAppTarget(slot, runningApps, installedPaths);

    if (auto context = findContextRow(slot, runningApps))
        return context;

    return resolveAppTarget(slot, runningApps, installedPaths);
}

std::vector<std::string>
    labelRowsWithSlotKeys(const std::vector<Slot>& slots,
                          const std::vector<std::string>& jumpChords,
                          const std::vector<RudderItem>& rows)
{
    auto labels = std::vector<std::string> {};
    labels.reserve(rows.size());

    for (auto& row: rows)
    {
        auto index = findSlotForRow(slots, row);
        labels.push_back(index ? getSlotKeyLabel(jumpChords, *index)
                               : std::string {});
    }

    return labels;
}

std::optional<SlotPress> findSlotPress(const Keybindings& bindings,
                                       std::uint16_t keyCode,
                                       const HeldModifiers& held)
{
    if (auto slot = findSlotForKeyPress(bindings.jumpToSlot, keyCode, held))
        return SlotPress {SlotChordAction::jump, *slot};

    if (auto slot = findSlotForKeyPress(bindings.putInSlot, keyCode, held))
        return SlotPress {SlotChordAction::put, *slot};

    return std::nullopt;
}

std::optional<std::string> describeSlotChordOwner(const Keybindings& bindings,
                                                  const Chord& chord)
{
    if (auto slot = findSlotForChord(bindings.jumpToSlot, chord))
        return describeSlot(*slot);

    if (auto slot = findSlotForChord(bindings.putInSlot, chord))
        return "putting in " + describeSlot(*slot);

    return std::nullopt;
}
} // namespace rudder
