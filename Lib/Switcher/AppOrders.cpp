#include "AppOrders.h"

#include <CowTermPalette/RankingMaterials.h>
#include <GrazerPalette/RankingMaterials.h>

#include <CowTermPalette/HostLabel.h>
#include <Miro/Reflection/Serialize.h>

#include <string_view>

namespace rudder
{
namespace
{
constexpr auto grazerPageContextPrefix = std::string_view {"url:"};
constexpr auto cowTermSessionContextPrefix = std::string_view {"session:"};
constexpr auto cowTermPlaceContextPrefix = std::string_view {"place:"};

RudderContext makeGrazerContext(const GoItem& row)
{
    auto isTab = row.tabId >= 0;
    auto context = RudderContext {};
    context.id = isTab ? std::to_string(row.tabId) : std::string {grazerPageContextPrefix} + row.url;
    context.title = row.title;
    context.subtitle = row.url;
    context.url = row.url;
    context.open = isTab;
    context.focused = row.isCurrent;
    return context;
}

std::string describeCowTermRow(const term::PaletteItem& row)
{
    if (row.host.empty())
        return row.detail;

    return term::remote::getHostLabel(row.host) + " · " + row.detail;
}

RudderContext makeCowTermContext(const term::PaletteItem& row)
{
    auto isSession = term::isSessionItem(row);
    auto prefix = isSession ? cowTermSessionContextPrefix : cowTermPlaceContextPrefix;
    auto context = RudderContext {};
    context.id = std::string {prefix} + row.key;
    context.title = row.label;
    context.subtitle = describeCowTermRow(row);
    context.open = isSession && !row.unavailable;

    if (row.host.empty())
        context.path = row.key;

    return context;
}

std::vector<RudderContext> orderContexts(const grazer::PaletteRankingMaterials& materials,
                                         const OrderingRequest& request)
{
    auto contexts = std::vector<RudderContext> {};

    for (auto& row: grazer::orderPalettePages(materials, request.query, request.nowMs))
        contexts.push_back(makeGrazerContext(row));

    return contexts;
}

std::vector<RudderContext> orderContexts(const term::PaletteRankingMaterials& materials,
                                         const OrderingRequest& request)
{
    auto contexts = std::vector<RudderContext> {};

    for (auto& row: term::orderPaletteItemsFromMaterials(materials, request.query, request.nowMs))
        contexts.push_back(makeCowTermContext(row));

    return contexts;
}

template <typename Materials>
AppOrdering makeOrdering(const protocol::RankingMaterials& reply)
{
    auto materials = Miro::createFromJSON<Materials>(reply.materials);

    return [materials = std::move(materials)](const OrderingRequest& request)
    { return orderContexts(materials, request); };
}
} // namespace

std::optional<AppOrdering> readAppOrdering(const protocol::RankingMaterials& reply)
{
    if (reply.ordering == grazer::paletteRankingMaterialsName)
        return makeOrdering<grazer::PaletteRankingMaterials>(reply);

    if (reply.ordering == term::paletteRankingMaterialsName)
        return makeOrdering<term::PaletteRankingMaterials>(reply);

    return std::nullopt;
}
} // namespace rudder
