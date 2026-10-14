#pragma once

#include "Items.h"

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rudder
{
// A place the user always wants one search away, from the config file. Each
// has a title and exactly one target: a url, an app (bundle id) or a path.
struct Favourite
{
    std::string title;
    std::string url;
    std::string app;
    std::string path;
    std::vector<std::string> keywords;

    // A url opens in this app (bundle id) instead of the default browser.
    std::string openWith;

    MIRO_REFLECT(title, url, app, path, keywords, openWith)
};

// The favourites document, ~/.config/rudder.json: pretty-printed and meant to
// be edited by hand (or by an agent) as much as from the panel.
struct FavouriteList
{
    std::vector<Favourite> favourites;

    MIRO_REFLECT(favourites)
};

struct FavouritesResult
{
    std::optional<FavouriteList> list;

    // Why `list` is empty, for the panel's footer.
    std::string error;
};

// Strict, field by field, so a hand edit with a wrong type or a missing title
// is an error naming it rather than a silently empty favourite. // and /* */
// comments are tolerated (Rudder's own writes drop them).
FavouritesResult parseFavourites(std::string_view text);

// Rows for the panel, in list order: kind "favourite", id "fav:<index>",
// the target in url / appId / path.
std::vector<RudderItem> favouriteItems(const FavouriteList& list);

// The favourite a row would become (an app row, a window's app, a tab's url),
// or nullopt for a row that can't be one.
std::optional<Favourite> favouriteFor(const RudderItem& item);

// Adds `favourite`, or removes it if one with the same target is there.
// True when it was added.
bool toggleFavourite(FavouriteList& list, const Favourite& favourite);

// For comparing a favourite's url with a provider's tab: no fragment, no
// trailing slash, scheme and host lower-cased.
std::string normaliseUrl(std::string_view url);

// The provider context already showing `url`, if any, as its context row.
std::optional<RudderItem> contextForUrl(const std::vector<RudderItem>& items,
                                        std::string_view url);
} // namespace rudder
