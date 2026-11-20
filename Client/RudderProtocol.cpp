#include "RudderProtocol.h"

#include <Miro/JSON/Json.h>
#include <Miro/Reflection/Serialize.h>

#include <algorithm>
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

// A usage field the app may leave out or send as null. `isWellFormed` is
// false for a field of the wrong type; `value` is null when it is left out.
struct ReportedField
{
    const Miro::JSON* value = nullptr;
    bool isWellFormed = true;
};

ReportedField findReportedField(const Object& object,
                                std::string_view key,
                                bool (Miro::JSON::*isRightType)() const)
{
    auto* value = Miro::Json::find(object, key);

    if (value == nullptr || value->isNull())
        return {};

    if (!(value->*isRightType)())
        return {nullptr, false};

    return {value, true};
}

bool readReportedInteger(const Object& object,
                         std::string_view key,
                         Miro::Omittable<std::int64_t>& out)
{
    auto field = findReportedField(object, key, &Miro::JSON::isInteger);

    if (field.value != nullptr)
        out = field.value->asInteger();

    return field.isWellFormed;
}

// A field every useCount carries.
bool readRequiredInteger(const Object& object, std::string_view key, std::int64_t& out)
{
    auto* value = Miro::Json::find(object, key);

    if (value == nullptr || !value->isInteger())
        return false;

    out = value->asInteger();
    return true;
}

bool readDecayingUseCount(const Object& object, Miro::Omittable<DecayingUseCount>& out)
{
    auto field = findReportedField(object, "useCount", &Miro::JSON::isObject);

    if (field.value == nullptr)
        return field.isWellFormed;

    auto& fields = field.value->asObject();
    auto* count = Miro::Json::find(fields, "count");
    auto useCount = DecayingUseCount {};

    if (count == nullptr || !count->isNumber())
        return false;

    useCount.count = count->asNumber();

    if (!readRequiredInteger(fields, "asOfMs", useCount.asOfMs))
        return false;

    if (!readRequiredInteger(fields, "halfLifeMs", useCount.halfLifeMs))
        return false;

    out = useCount;
    return true;
}

using ReportedTime = Miro::Omittable<std::int64_t> ContextUsage::*;

const std::vector<std::pair<std::string_view, ReportedTime>> reportedIntegerFields {
    {"lastUsedAtMs", &ContextUsage::lastUsedAtMs},
    {"lastVisitAtMs", &ContextUsage::lastVisitAtMs},
    {"visitCount", &ContextUsage::visitCount},
    {"lastTypedAtMs", &ContextUsage::lastTypedAtMs},
    {"lastFileChangeAtMs", &ContextUsage::lastFileChangeAtMs},
};

bool readContextUsage(const Object& object, Miro::Omittable<ContextUsage>& out)
{
    auto field = findReportedField(object, "usage", &Miro::JSON::isObject);

    if (field.value == nullptr)
        return field.isWellFormed;

    auto& fields = field.value->asObject();
    auto usage = ContextUsage {};

    for (auto& [key, member]: reportedIntegerFields)
        if (!readReportedInteger(fields, key, usage.*member))
            return false;

    if (!readDecayingUseCount(fields, usage.useCount))
        return false;

    out = std::move(usage);
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
           && readFields(fields, contextBoolFields, context, readBool)
           && readContextUsage(fields, context.usage);
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

std::optional<Message> readClose(const Object& object)
{
    auto close = Close {};

    if (!readString(object, "contextId", close.contextId) || close.contextId.empty())
        return std::nullopt;

    return close;
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
            else if constexpr (std::is_same_v<T, Activate>)
                return withType(payload, "activate");
            else
                return withType(payload, "close");
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

    if (name == "close")
        return readClose(object);

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
