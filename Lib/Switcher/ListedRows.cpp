#include "ListedRows.h"

#include <algorithm>

namespace rudder
{
std::vector<RudderItem> refreshListedRowsInPlace(std::vector<RudderItem> listed,
                                                 const std::vector<RudderItem>& fresh)
{
    for (auto& row: listed)
    {
        auto same = std::find_if(fresh.begin(),
                                 fresh.end(),
                                 [&](const RudderItem& other) { return other.id == row.id; });

        if (same != fresh.end())
            row = *same;
    }

    return listed;
}
} // namespace rudder
