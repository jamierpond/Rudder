#pragma once

#include "Items.h"

#include <RudderProtocol.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rudder
{
// The typed text and the moment an app's rows are ordered for.
//
// An app's own ordering of its rows, run on the ranking materials it handed
// Rudder: the same PondDev function the app's palette runs. Its rows come
// back as contexts Rudder lists and activates: a Grazer tab keeps its tab id,
// any other Grazer page is "url:<url>"; a CowTerm session is "session:<key>",
// a project "place:<key>".
struct OrderingRequest
{
    std::string query;
    std::int64_t nowMs = 0;
};

using AppOrdering = std::function<std::vector<RudderContext>(const OrderingRequest& request)>;

// Nothing for an ordering Rudder doesn't know.
std::optional<AppOrdering> readAppOrdering(const protocol::RankingMaterials& reply);

// Each app's rows for the typed text in its own order, by app id.
using AppOrderedContexts = std::map<std::string, std::vector<RudderContext>>;
} // namespace rudder
