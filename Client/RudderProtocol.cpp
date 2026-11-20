#include "RudderProtocol.h"

#include <Miro/JSON/Json.h>
#include <Miro/Reflection/Serialize.h>

#include <algorithm>
#include <functional>
#include <map>
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

const std::vector<std::pair<std::string_view, std::string Context::*>> contextStringFields {
    {"title", &Context::title},
    {"subtitle", &Context::subtitle},
    {"url", &Context::url},
    {"path", &Context::path},
    {"branch", &Context::branch},
    {"repo", &Context::repo},
};

const std::vector<std::pair<std::string_view, bool Context::*>> contextBoolFields {
    {"open", &Context::open},
    {"focused", &Context::focused},
};

template <typename FieldType, typename ReadField>
bool readFields(const Object& fields,
                const std::vector<std::pair<std::string_view, FieldType Context::*>>& table,
                Context& context,
                ReadField readField)
{
    return std::all_of(table.begin(),
                       table.end(),
                       [&](const auto& entry)
                       { return readField(fields, entry.first, context.*entry.second); });
}

bool readContext(const Object& fields, Context& context)
{
    if (!readString(fields, "id", context.id) || context.id.empty())
        return false;

    return readFields(fields, contextStringFields, context, readString)
           && readFields(fields, contextBoolFields, context, readBool);
}

std::optional<Message> readHello(const Object& object)
{
    auto hello = Hello {};

    if (!readString(object, "app", hello.app) || !readString(object, "bundleId", hello.bundleId)
        || hello.bundleId.empty() || !readBool(object, "canClose", hello.canClose))
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

        auto context = Context {};

        if (!readContext(entry.asObject(), context))
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

constexpr auto rankingMaterialsRequestType = "rankingMaterialsRequest";
constexpr auto rankingMaterialsType = "rankingMaterials";

Miro::JSON encodePayload(const Hello& hello) { return withType(hello, "hello"); }
Miro::JSON encodePayload(const Contexts& contexts) { return withType(contexts, "contexts"); }
Miro::JSON encodePayload(const Activate& activate) { return withType(activate, "activate"); }
Miro::JSON encodePayload(const Close& close) { return withType(close, "close"); }

Miro::JSON encodePayload(const RankingMaterialsRequest&)
{
    auto json = Miro::JSON {Object {}};
    json.toObject()["type"] = rankingMaterialsRequestType;
    return json;
}

Miro::JSON encodePayload(const RankingMaterials& reply)
{
    return withType(reply, rankingMaterialsType);
}

std::optional<Message> readRankingMaterials(const Object& object)
{
    auto reply = RankingMaterials {};

    if (!readString(object, "ordering", reply.ordering) || reply.ordering.empty())
        return std::nullopt;

    auto* materials = Miro::Json::find(object, "materials");

    if (materials == nullptr || !materials->isObject())
        return std::nullopt;

    reply.materials = *materials;
    return reply;
}

std::optional<Message> readRankingMaterialsRequest(const Object&)
{
    return RankingMaterialsRequest {};
}

std::optional<Message> readClose(const Object& object)
{
    auto close = Close {};

    if (!readString(object, "contextId", close.contextId) || close.contextId.empty())
        return std::nullopt;

    return close;
}

using MessageReader = std::optional<Message> (*)(const Object&);

const std::map<std::string, MessageReader, std::less<>> messageReaders {
    {"hello", readHello},
    {"contexts", readContexts},
    {"activate", readActivate},
    {"close", readClose},
    {rankingMaterialsRequestType, readRankingMaterialsRequest},
    {rankingMaterialsType, readRankingMaterials},
};
} // namespace

std::string encode(const Message& message)
{
    auto json = std::visit([](const auto& payload) { return encodePayload(payload); }, message);
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

    auto reader = messageReaders.find(type->asString());

    if (reader == messageReaders.end())
        return std::nullopt;

    return reader->second(object);
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
