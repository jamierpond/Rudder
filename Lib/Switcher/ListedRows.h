#pragma once

#include "Items.h"

#include <vector>

namespace rudder
{
// The panel's rows stay put until the next keystroke or summon: each row in
// `listed` takes its live data from the row with the same id in `fresh`
// (a fresh ranking), and a row `fresh` lacks stays as it was. Rows only in
// `fresh` aren't added.
std::vector<RudderItem> refreshListedRowsInPlace(std::vector<RudderItem> listed,
                                                 const std::vector<RudderItem>& fresh);
} // namespace rudder
