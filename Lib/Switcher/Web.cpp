#include "Web.h"
#include "Favourites.h"
#include "Ranking.h"
#include "Url.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_set>

namespace rudder
{
namespace
{
std::string_view trimmed(std::string_view text)
{
    auto first = text.find_first_not_of(' ');

    if (first == std::string_view::npos)
        return {};

    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

std::string lowered(std::string_view text)
{
    auto out = std::string {text};

    for (auto& c: out)
        c = (char) std::tolower((unsigned char) c);

    return out;
}

// scheme "://" at the very start, RFC 3986 scheme characters.
bool hasScheme(std::string_view text)
{
    auto colon = text.find("://");

    if (colon == std::string_view::npos || colon == 0
        || !std::isalpha((unsigned char) text[0]))
        return false;

    return std::all_of(text.begin(),
                       text.begin() + (long) colon,
                       [](char c)
                       {
                           return std::isalnum((unsigned char) c) || c == '+' || c == '-'
                                  || c == '.';
                       });
}

bool allDigits(std::string_view text)
{
    return !text.empty()
           && std::all_of(text.begin(),
                          text.end(),
                          [](char c) { return std::isdigit((unsigned char) c); });
}

std::vector<std::string_view> split(std::string_view text, char separator)
{
    auto parts = std::vector<std::string_view> {};

    while (true)
    {
        auto at = text.find(separator);
        parts.push_back(text.substr(0, at));

        if (at == std::string_view::npos)
            return parts;

        text.remove_prefix(at + 1);
    }
}

bool isIPv4(std::string_view host)
{
    auto octets = split(host, '.');

    return octets.size() == 4
           && std::all_of(octets.begin(),
                          octets.end(),
                          [](std::string_view octet)
                          { return allDigits(octet) && octet.size() <= 3 && std::stoi(std::string {octet}) <= 255; });
}

// Generic TLDs people actually type, plus every two-letter country code.
const std::unordered_set<std::string>& topLevelDomains()
{
    static const auto domains = []
    {
        auto set = std::unordered_set<std::string> {
            "com",    "org",   "net",   "edu",     "gov",    "mil",    "int",
            "info",   "biz",   "app",   "dev",     "page",   "xyz",    "online",
            "site",   "tech",  "store", "shop",    "blog",   "cloud",  "link",
            "live",   "news",  "club",  "design",  "art",    "space",  "website",
            "wiki",   "one",   "top",   "inc",     "studio", "agency", "games",
            "today",  "world", "email", "codes",   "tools",  "run",    "foo",
            "zone",   "media", "works", "systems", "social", "chat",   "new",
            "google", "local", "arpa"};

        // IANA's two-letter country codes.
        auto countries = std::string_view {
            "ac ad ae af ag ai al am ao aq ar as at au aw ax az ba bb bd be bf bg bh bi "
            "bj bm bn bo br bs bt bw by bz ca cc cd cf cg ch ci ck cl cm cn co cr cu cv "
            "cw cx cy cz de dj dk dm do dz ec ee eg er es et eu fi fj fk fm fo fr ga gb "
            "gd ge gf gg gh gi gl gm gn gp gq gr gs gt gu gw gy hk hm hn hr ht hu id ie "
            "il im in io iq ir is it je jm jo jp ke kg kh ki km kn kp kr kw ky kz la lb "
            "lc li lk lr ls lt lu lv ly ma mc md me mg mh mk ml mm mn mo mp mq mr ms mt "
            "mu mv mw mx my mz na nc ne nf ng ni nl no np nr nu nz om pa pe pf pg ph pk "
            "pl pm pn pr ps pt pw py qa re ro rs ru rw sa sb sc sd se sg sh si sk sl sm "
            "sn so sr ss st su sv sx sy sz tc td tf tg th tj tk tl tm tn to tr tt tv tw "
            "tz ua ug uk us uy uz va vc ve vg vi vn vu wf ws ye yt za zm zw"};

        for (auto code: split(countries, ' '))
            set.insert(std::string {code});

        return set;
    }();

    return domains;
}

// Country codes that are also everyday file extensions: "main.py" is a
// file name far more often than a Paraguayan site.
bool readsAsFileExtension(std::string_view tld)
{
    static const auto extensions =
        std::unordered_set<std::string_view> {"sh", "py", "md", "rs", "pl", "cc", "cs", "mm",
                                              "so", "ps", "hh", "in", "am", "ml"};
    return extensions.contains(tld);
}

bool isLabel(std::string_view label)
{
    return !label.empty() && label.front() != '-' && label.back() != '-'
           && std::all_of(label.begin(),
                          label.end(),
                          [](char c)
                          {
                              auto byte = (unsigned char) c;
                              return std::isalnum(byte) || c == '-' || byte >= 0x80;
                          });
}

struct Host
{
    std::string_view name;
    bool hasPort = false;
    bool hasRest = false;
    bool valid = false;
};

// The host of a scheme-less address, with whether a :port or a path, query
// or fragment follows it.
Host hostOf(std::string_view address)
{
    auto host = Host {};
    auto end = address.find_first_of("/?#");
    host.hasRest = end != std::string_view::npos;
    auto authority = address.substr(0, end);

    if (auto colon = authority.find(':'); colon != std::string_view::npos)
    {
        if (!allDigits(authority.substr(colon + 1)))
            return host;

        host.hasPort = true;
        authority = authority.substr(0, colon);
    }

    host.name = authority;
    host.valid = !authority.empty();
    return host;
}

bool isLocal(const Host& host)
{
    return lowered(host.name) == "localhost" || isIPv4(host.name);
}

// What the search engine is called in the row: Google, or the host of a
// custom searchUrl.
std::string engineName(std::string_view searchUrl)
{
    if (searchUrl.empty())
        return "Google";

    auto start = searchUrl.find("://");
    auto host = searchUrl.substr(start == std::string_view::npos ? 0 : start + 3);
    host = host.substr(0, host.find_first_of("/?#:"));

    if (host.starts_with("www."))
        host.remove_prefix(4);

    if (host == "google.com" || host.starts_with("google."))
        return "Google";

    return std::string {host};
}
} // namespace

bool looksLikeAddress(std::string_view query)
{
    auto text = std::string {trimmed(query)};

    if (!wim::looksLikeURL(text))
        return false;

    if (text.find("://") != std::string::npos)
        return hasScheme(text);

    auto host = hostOf(text);

    if (!host.valid)
        return false;

    if (isLocal(host))
        return true;

    auto labels = split(host.name, '.');

    if (labels.size() < 2
        || !std::all_of(labels.begin(), labels.end(), [](auto label) { return isLabel(label); }))
        return false;

    auto tld = lowered(labels.back());

    if (!topLevelDomains().contains(tld))
        return false;

    if (readsAsFileExtension(tld))
        return host.hasPort || host.hasRest || labels.size() > 2;

    return true;
}

std::string addressUrl(std::string_view address)
{
    auto text = std::string {trimmed(address)};

    if (hasScheme(text))
        return text;

    if (isLocal(hostOf(text)))
        return "http://" + text;

    return wim::withScheme(text);
}

std::string searchUrlFor(std::string_view query, std::string_view searchUrl)
{
    auto url = std::string {searchUrl.empty() ? defaultSearchUrl : searchUrl};
    auto encoded = wim::urlEncode(std::string {trimmed(query)});

    for (auto at = url.find("%s"); at != std::string::npos; at = url.find("%s", at))
    {
        url.replace(at, 2, encoded);
        at += encoded.size();
    }

    return url;
}

std::string browserFor(std::string_view openWith, std::string_view browser)
{
    return std::string {!openWith.empty() ? openWith : browser};
}

bool weakMatch(std::string_view query, const std::vector<int>& scores)
{
    if (scores.empty())
        return true;

    auto typed = (int) std::count_if(query.begin(), query.end(), [](char c) { return c != ' '; });
    return *std::max_element(scores.begin(), scores.end()) < weakScorePerChar * typed;
}

RudderItem webRow(std::string_view query, const WebSettings& settings)
{
    auto text = std::string {trimmed(query)};
    auto row = RudderItem {};
    row.kind = "web";
    row.id = "web:" + (settings.browser.empty() ? std::string {"default"} : settings.browser);

    if (looksLikeAddress(text))
    {
        row.title = "Open " + text;
        row.url = addressUrl(text);
    }
    else
    {
        row.title = "Search " + engineName(settings.searchUrl) + " for “" + text + "”";
        row.url = searchUrlFor(text, settings.searchUrl);
    }

    row.subtitle = row.url;
    return row;
}

std::vector<RudderItem> rankWithWeb(const std::vector<RudderItem>& all,
                                    const std::string& query,
                                    const WebSettings& settings)
{
    if (trimmed(query).empty())
        return rankItems(all, query);

    auto scored = scoreItems(all, query);
    auto ranked = std::vector<RudderItem> {};
    auto scores = std::vector<int> {};

    for (auto& [score, item]: scored)
    {
        scores.push_back(item.kind == "favourite" ? score - favouriteBoost : score);
        ranked.push_back(std::move(item));
    }

    if (looksLikeAddress(query))
    {
        // Typing an address that is already a favourite or an open tab picks
        // that row: it reuses what's open rather than opening it again.
        auto wanted = normaliseUrl(addressUrl(query));
        auto exact = std::optional<RudderItem> {};

        for (auto& item: all)
            if (item.kind == "favourite" && !item.url.empty() && normaliseUrl(item.url) == wanted)
            {
                exact = item;
                break;
            }

        if (!exact)
            exact = contextForUrl(all, wanted);

        if (exact)
        {
            auto found = std::find_if(ranked.begin(),
                                      ranked.end(),
                                      [&](const RudderItem& item) { return item.id == exact->id; });

            if (found != ranked.end())
                std::rotate(ranked.begin(), found, found + 1);
            else
                ranked.insert(ranked.begin(), *exact);

            return ranked;
        }

        ranked.insert(ranked.begin(), webRow(query, settings));
        return ranked;
    }

    if (weakMatch(query, scores))
        ranked.insert(ranked.begin(), webRow(query, settings));
    else
        ranked.push_back(webRow(query, settings));

    return ranked;
}
} // namespace rudder
