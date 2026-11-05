#include <ListedRows.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
RudderItem row(const std::string& id, const std::string& title)
{
    auto item = RudderItem {};
    item.id = id;
    item.title = title;
    return item;
}

std::vector<std::string> idsOf(const std::vector<RudderItem>& rows)
{
    auto ids = std::vector<std::string> {};

    for (auto& item: rows)
        ids.push_back(item.id);

    return ids;
}
} // namespace

auto tRowsKeepOrder = test("ListedRows/a push reordering the ranking leaves the rows in place") = []
{
    auto listed = std::vector {row("a", "A"), row("b", "B"), row("c", "C")};
    auto fresh = std::vector {row("c", "C"), row("a", "A"), row("b", "B")};

    check(idsOf(refreshListedRowsInPlace(listed, fresh))
          == std::vector<std::string> {"a", "b", "c"});
};

auto tRowsRefreshData = test("ListedRows/a listed row takes its fresh title") = []
{
    auto refreshed =
        refreshListedRowsInPlace({row("a", "old"), row("b", "B")}, {row("a", "new")});

    check(refreshed[0].title == "new");
};

auto tRowsNotAddedOrRemoved
    = test("ListedRows/a row gone from the ranking stays; a new one isn't added") = []
{
    auto refreshed =
        refreshListedRowsInPlace({row("a", "A"), row("b", "B")}, {row("b", "B"), row("n", "N")});

    check(idsOf(refreshed) == std::vector<std::string> {"a", "b"});
};
