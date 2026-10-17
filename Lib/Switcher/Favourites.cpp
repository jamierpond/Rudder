#include "Favourites.h"

#include <Miro/JSON/Json.h>
#include <Miro/Reflection/Serialize.h>

#include <algorithm>
#include <cctype>

namespace rudder
{
namespace
{
// Drops // and /* */ comments outside strings, keeping newlines so a parse
// error's position still means something.
std::string stripComments(std::string_view text)
{
    auto out = std::string {};
    auto inString = false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        auto c = text[i];

        if (inString)
        {
            out += c;

            if (c == '\\' && i + 1 < text.size())
                out += text[++i];
            else if (c == '"')
                inString = false;

            continue;
        }

        if (c == '"')
        {
            inString = true;
            out += c;
            continue;
        }

        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n')
                ++i;

            out += '\n';
            continue;
        }

        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            i += 2;

            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/'))
            {
                if (text[i] == '\n')
                    out += '\n';

                ++i;
            }

            ++i;
            continue;
        }

        out += c;
    }

    return out;
}

std::string lowered(std::string text)
{
    for (auto& c: text)
        c = (char) std::tolower((unsigned char) c);

    return text;
}

using Object = Miro::Json::Object;

std::string typeError(std::size_t index, const char* field, const char* type)
{
    return "favourite " + std::to_string(index + 1) + ": \"" + field + "\" must be "
           + type;
}

// Field by field, so a wrong type is an error that names it rather than a
// silently empty value.
std::optional<Favourite> readFavourite(const Miro::JSON& json,
                                       std::size_t index,
                                       std::string& error)
{
    if (!json.isObject())
    {
        error = "favourite " + std::to_string(index + 1) + " must be an object";
        return std::nullopt;
    }

    auto& object = json.asObject();
    auto favourite = Favourite {};

    for (auto [field, out]: {std::pair {"title", &favourite.title},
                             std::pair {"url", &favourite.url},
                             std::pair {"app", &favourite.app},
                             std::pair {"path", &favourite.path},
                             std::pair {"openWith", &favourite.openWith}})
    {
        if (auto* value = Miro::Json::find(object, field))
        {
            if (!value->isString())
            {
                error = typeError(index, field, "a string");
                return std::nullopt;
            }

            *out = value->asString();
        }
    }

    if (auto* keywords = Miro::Json::find(object, "keywords"))
    {
        if (!keywords->isArray())
        {
            error = typeError(index, "keywords", "a list of strings");
            return std::nullopt;
        }

        for (auto& keyword: keywords->asArray())
        {
            if (!keyword.isString())
            {
                error = typeError(index, "keywords", "a list of strings");
                return std::nullopt;
            }

            favourite.keywords.push_back(keyword.asString());
        }
    }

    auto targets = (int) !favourite.url.empty() + (int) !favourite.app.empty()
                   + (int) !favourite.path.empty();

    if (favourite.title.empty())
        error = "favourite " + std::to_string(index + 1) + " needs a \"title\"";
    else if (targets != 1)
        error = "favourite \"" + favourite.title
                + "\" needs exactly one of \"url\", \"app\" or \"path\"";

    if (!error.empty())
        return std::nullopt;

    return favourite;
}

// {"<screenKey>": {"x", "y", "width", "height"}}, every field a number and the
// size positive, so a typo is an error rather than a panel at 0,0.
std::optional<std::map<std::string, Frame>> readPanelFrames(const Miro::JSON& json,
                                                            std::string& error)
{
    if (!json.isObject())
    {
        error = "\"panel\" must be an object of screens";
        return std::nullopt;
    }

    auto frames = std::map<std::string, Frame> {};

    for (auto& [key, value]: json.asObject())
    {
        auto where = "panel \"" + key + "\"";

        if (!value.isObject())
        {
            error = where + " must be an object";
            return std::nullopt;
        }

        auto frame = Frame {};

        for (auto [field, out]: {std::pair {"x", &frame.x},
                                 std::pair {"y", &frame.y},
                                 std::pair {"width", &frame.width},
                                 std::pair {"height", &frame.height}})
        {
            auto* number = Miro::Json::find(value.asObject(), field);

            if (number == nullptr || !number->isNumber())
            {
                error = where + ": \"" + field + "\" must be a number";
                return std::nullopt;
            }

            *out = (float) number->asNumber();
        }

        if (frame.width <= 0.f || frame.height <= 0.f)
        {
            error = where + ": \"width\" and \"height\" must be above 0";
            return std::nullopt;
        }

        frames[key] = frame;
    }

    return frames;
}
} // namespace

FavouritesResult parseFavourites(std::string_view text)
{
    auto json = Miro::JSON {};

    try
    {
        json = Miro::Json::parse(stripComments(text));
    }
    catch (const std::exception& error)
    {
        return {std::nullopt, std::string {"rudder.json: "} + error.what()};
    }

    if (!json.isObject())
        return {std::nullopt, "rudder.json: the top level must be an object"};

    auto config = FavouriteList {};

    for (auto [field, out]: {std::pair {"browser", &config.browser},
                             std::pair {"searchUrl", &config.searchUrl}})
    {
        if (auto* value = Miro::Json::find(json.asObject(), field))
        {
            if (!value->isString())
                return {std::nullopt,
                        std::string {"rudder.json: \""} + field + "\" must be a string"};

            *out = value->asString();
        }
    }

    if (!config.searchUrl.empty() && config.searchUrl.find("%s") == std::string::npos)
        return {std::nullopt,
                "rudder.json: \"searchUrl\" needs a %s where the query goes"};

    if (auto* panel = Miro::Json::find(json.asObject(), "panel"))
    {
        auto error = std::string {};
        auto frames = readPanelFrames(*panel, error);

        if (!frames)
            return {std::nullopt, "rudder.json: " + error};

        config.panel = std::move(*frames);
    }

    auto* favourites = Miro::Json::find(json.asObject(), "favourites");

    if (favourites == nullptr)
        return {config, {}};

    if (!favourites->isArray())
        return {std::nullopt, "rudder.json: \"favourites\" must be a list"};

    auto index = std::size_t {0};

    for (auto& entry: favourites->asArray())
    {
        auto error = std::string {};
        auto favourite = readFavourite(entry, index++, error);

        if (!favourite)
            return {std::nullopt, "rudder.json: " + error};

        config.favourites.push_back(std::move(*favourite));
    }

    return {config, {}};
}

bool adoptPanelFrames(FavouriteList& config, std::string_view oldText, std::string& error)
{
    if (config.panel)
        return false;

    auto json = Miro::JSON {};

    try
    {
        json = Miro::Json::parse(std::string {oldText});
    }
    catch (const std::exception& failure)
    {
        error = std::string {"panel-frames.json: "} + failure.what();
        return false;
    }

    auto frames = readPanelFrames(json, error);

    if (!frames)
    {
        error = "panel-frames.json: " + error;
        return false;
    }

    if (frames->empty())
        return false;

    config.panel = std::move(*frames);
    return true;
}

std::vector<RudderItem> favouriteItems(const FavouriteList& config)
{
    auto items = std::vector<RudderItem> {};

    for (std::size_t i = 0; i < config.favourites.size(); ++i)
    {
        auto& favourite = config.favourites[i];
        auto item = RudderItem {};
        item.kind = "favourite";
        item.id = "fav:" + std::to_string(i);
        item.title = favourite.title;
        item.url = favourite.url;
        item.appId = favourite.app;
        item.path = favourite.path;
        item.keywords = favourite.keywords;
        item.openWith = favourite.openWith;
        item.subtitle = !favourite.url.empty() ? favourite.url
                        : !favourite.app.empty() ? favourite.app
                                                 : favourite.path;
        items.push_back(std::move(item));
    }

    return items;
}

std::optional<Favourite> favouriteFor(const RudderItem& item)
{
    auto favourite = Favourite {};

    if (item.kind == "favourite")
    {
        favourite.title = item.title;
        favourite.url = item.url;
        favourite.app = item.url.empty() && item.path.empty() ? item.appId : "";
        favourite.path = item.path;
        return favourite;
    }

    // A tab (or any provider context) with a url is that url; otherwise the
    // row stands for its app.
    if (item.kind == "context" && !item.url.empty())
    {
        favourite.title = item.title;
        favourite.url = item.url;
        return favourite;
    }

    if (item.appId.empty() || item.appId.starts_with("pid:"))
        return std::nullopt;

    favourite.title = item.kind == "app" ? item.title : item.subtitle;
    favourite.app = item.appId;
    return favourite;
}

bool toggleFavourite(FavouriteList& list, const Favourite& favourite)
{
    auto sameTarget = [&](const Favourite& other)
    {
        if (!favourite.url.empty())
            return normaliseUrl(other.url) == normaliseUrl(favourite.url);

        if (!favourite.app.empty())
            return other.app == favourite.app;

        return !favourite.path.empty() && other.path == favourite.path;
    };

    if (std::erase_if(list.favourites, sameTarget) > 0)
        return false;

    list.favourites.push_back(favourite);
    return true;
}

std::string normaliseUrl(std::string_view url)
{
    auto text = std::string {url.substr(0, url.find('#'))};

    while (!text.empty() && text.back() == '/')
        text.pop_back();

    // Scheme and host are case-insensitive; the path isn't.
    auto scheme = text.find("://");
    auto hostEnd = scheme == std::string::npos ? std::string::npos
                                                : text.find('/', scheme + 3);
    auto prefix = hostEnd == std::string::npos ? text.size() : hostEnd;

    return lowered(text.substr(0, prefix)) + text.substr(prefix);
}

std::optional<RudderItem> contextForUrl(const std::vector<RudderItem>& items,
                                        std::string_view url)
{
    auto wanted = normaliseUrl(url);

    for (auto& app: items)
    {
        if (app.kind != "app" || !app.running)
            continue;

        for (auto& context: app.contexts)
        {
            if (context.kind != "context" || context.url.empty()
                || normaliseUrl(context.url) != wanted)
                continue;

            auto row = RudderItem {};
            row.kind = "context";
            row.id = app.id + "/" + context.id;
            row.title = context.title;
            row.subtitle = app.title;
            row.appId = app.id;
            row.contextId = context.id;
            row.url = context.url;
            return row;
        }
    }

    return std::nullopt;
}
} // namespace rudder
