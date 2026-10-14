#pragma once

#include "Items.h"

#include <string>
#include <string_view>
#include <vector>

namespace rudder
{
// Rudder's way onto the web: a typed address opens, anything else searches.
// Built on the omnibox functions ported from Wim (Url.h); the extra
// strictness here is Rudder's, since a switcher query is far more often an
// app or a file name than a browser's is.

constexpr auto defaultSearchUrl = "https://www.google.com/search?q=%s";

// The rudder.json settings the web row needs.
struct WebSettings
{
    // Bundle id links open in; empty is the system default browser.
    std::string browser;

    // %s stands for the query; empty is defaultSearchUrl.
    std::string searchUrl;
};

// Whether a query is an address to go to rather than words to search for:
// a scheme ("https://x", "file:///tmp"), localhost[:port][/path], an IPv4
// address[:port][/path], or a spaceless host whose last label is a real
// top-level domain (github.com, news.ycombinator.com/item?id=1). Wim takes
// any dotted token; Rudder also wants the TLD, so "node.js" is a search.
// A few country codes that read as file extensions (sh, py, md, rs, ...)
// only count with a path, a port or a third label: "main.py" is a search,
// "docs.python.org" isn't affected and "bun.sh/docs" opens.
bool looksLikeAddress(std::string_view query);

// The url an address opens: as typed with a scheme, http:// for localhost
// and IP addresses (dev servers rarely do TLS), https:// otherwise.
std::string addressUrl(std::string_view address);

// `searchUrl` (defaultSearchUrl when empty) with every %s replaced by the
// url-encoded query.
std::string searchUrlFor(std::string_view query, std::string_view searchUrl);

// The app a link opens in: its own openWith, else the configured browser,
// else "" -- the system default.
std::string browserFor(std::string_view openWith, std::string_view browser);

// Under this many points per typed character (spaces aside) a match is weak:
// a run of prefix letters scores about 9 per character, a single leading
// letter 5, while letters scattered across a long name score 0-2 per
// character.
constexpr auto weakScorePerChar = 4;

// Whether the best of `scores` -- raw match scores, favouriteBoost taken
// off -- is weak for `query`. No scores at all is weak.
bool weakMatch(std::string_view query, const std::vector<int>& scores);

// The row Enter turns into a page: "Open github.com" or
// "Search Google for “query”". Kind "web", never previewed.
RudderItem webRow(std::string_view query, const WebSettings& settings);

// rankItems plus the web row, for any query with something besides spaces:
// first (and so selected) when nothing matches well or the query is an
// address, last otherwise. An address that a favourite or an open tab
// already shows puts that row first instead, with no web row.
std::vector<RudderItem> rankWithWeb(const std::vector<RudderItem>& all,
                                    const std::string& query,
                                    const WebSettings& settings);
} // namespace rudder
