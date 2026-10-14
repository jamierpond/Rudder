#include "RudderProtocol.h"

#include <Miro/JSON/Json.h>
#include <Miro/Reflection/Serialize.h>

#include <utility>

namespace rudder::protocol
{
namespace
{
template <typename T>
Miro::JSON withType(const T& payload, const char* type)
{
    auto json = Miro::toJSON(payload);
    json.toObject()["type"] = type;
    return json;
}

using Object = Miro::Json::Object;

// A field that must be a string when present; `out` keeps its default when
// it's absent. False for a field of the wrong type.
bool readString(const Object& object, std::string_view key, std::string& out)
{
    auto* value = Miro::Json::find(object, key);

    if (value == nullptr)
        return true;

    if (!value->isString())
        return false;

    out = value->asString();
    return true;
}

// The same for a bool.
bool readBool(const Object& object, std::string_view key, bool& out)
{
    auto* value = Miro::Json::find(object, key);

    if (value == nullptr)
        return true;

    if (!value->isBool())
        return false;

    out = value->asBool();
    return true;
}

std::optional<Message> readHello(const Object& object)
{
    auto hello = Hello {};

    if (!readString(object, "app", hello.app) || !readString(object, "bundleId", hello.bundleId)
        || hello.bundleId.empty())
        return std::nullopt;

    if (auto* pid = Miro::Json::find(object, "pid"))
    {
        if (!pid->isInteger())
            return std::nullopt;

        hello.pid = pid->asInteger();
    }

    return hello;
}

std::optional<Message> readContexts(const Object& object)
{
    auto* list = Miro::Json::find(object, "contexts");

    if (list == nullptr || !list->isArray())
        return std::nullopt;

    auto contexts = Contexts {};

    for (auto& entry: list->asArray())
    {
        if (!entry.isObject())
            return std::nullopt;

        auto& fields = entry.asObject();
        auto context = Context {};

        if (!readString(fields, "id", context.id) || context.id.empty()
            || !readString(fields, "title", context.title)
            || !readString(fields, "subtitle", context.subtitle)
            || !readString(fields, "url", context.url)
            || !readString(fields, "path", context.path)
            || !readBool(fields, "open", context.open)
            || !readBool(fields, "focused", context.focused))
            return std::nullopt;

        contexts.contexts.push_back(std::move(context));
    }

    return contexts;
}

std::optional<Message> readActivate(const Object& object)
{
    auto activate = Activate {};

    if (!readString(object, "contextId", activate.contextId) || activate.contextId.empty())
        return std::nullopt;

    if (auto* raise = Miro::Json::find(object, "raise"))
    {
        if (!raise->isBool())
            return std::nullopt;

        activate.raise = raise->asBool();
    }

    return activate;
}
} // namespace

std::string encode(const Message& message)
{
    auto json = std::visit(
        [](const auto& payload)
        {
            using T = std::decay_t<decltype(payload)>;

            if constexpr (std::is_same_v<T, Hello>)
                return withType(payload, "hello");
            else if constexpr (std::is_same_v<T, Contexts>)
                return withType(payload, "contexts");
            else
                return withType(payload, "activate");
        },
        message);

    return Miro::Json::print(json) + "\n";
}

std::optional<Message> decode(std::string_view line)
{
    if (line.size() > maxLineBytes)
        return std::nullopt;

    auto json = Miro::Json::getParsedValue(line);

    if (!json.isObject())
        return std::nullopt;

    auto& object = json.asObject();
    auto* type = Miro::Json::find(object, "type");

    if (type == nullptr || !type->isString())
        return std::nullopt;

    auto& name = type->asString();

    if (name == "hello")
        return readHello(object);

    if (name == "contexts")
        return readContexts(object);

    if (name == "activate")
        return readActivate(object);

    return std::nullopt;
}

std::vector<std::string> LineBuffer::append(std::string_view bytes)
{
    auto lines = std::vector<std::string> {};

    for (auto c: bytes)
    {
        if (c == '\n')
        {
            if (!discarding && !pending.empty())
                lines.push_back(std::move(pending));

            pending.clear();
            discarding = false;
            continue;
        }

        if (discarding)
            continue;

        pending += c;

        if (pending.size() > maxLineBytes)
        {
            pending.clear();
            discarding = true;
        }
    }

    return lines;
}
} // namespace rudder::protocol
