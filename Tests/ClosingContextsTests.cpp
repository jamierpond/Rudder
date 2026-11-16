#include <ClosingContexts.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

namespace
{
constexpr std::int64_t closedAtMs = 1'000'000;
constexpr auto grazerId = "audio.pond.grazer";

RudderItem makeTabRow(const std::string& contextId)
{
    auto row = RudderItem {};
    row.kind = "context";
    row.id = std::string {grazerId} + "/" + contextId;
    row.appId = grazerId;
    row.contextId = contextId;
    return row;
}

std::vector<RudderContext> makeTabs(const std::vector<std::string>& ids)
{
    auto tabs = std::vector<RudderContext> {};

    for (auto& id: ids)
        tabs.push_back({id, "Tab " + id, "", ""});

    return tabs;
}
} // namespace

auto tRowGoesAtOnce = test("ClosingContexts/a closed tab's row leaves the list at once") = []
{
    auto closing = ClosingContexts {};
    auto rows = std::vector<RudderItem> {makeTabRow("1"), makeTabRow("2"), makeTabRow("3")};

    closing.markClosing(grazerId, "2", closedAtMs);
    closing.removeClosingRows(rows, closedAtMs);

    check(rows.size() == 2);
    check(rows[1].contextId == "3", "the next row takes the closed row's place");
};

auto tStalePushDoesntResurrect = test("ClosingContexts/a push sent before the close doesn't bring the tab back") = []
{
    auto closing = ClosingContexts {};
    closing.markClosing(grazerId, "2", closedAtMs);

    auto pushed = closing.removeClosingContexts(grazerId, makeTabs({"1", "2"}), closedAtMs + 10);

    check(pushed.size() == 1);
    check(pushed.front().id == "1");
};

auto tDoneOncePushDropsIt = test("ClosingContexts/once a push drops the tab, a reused id shows again") = []
{
    auto closing = ClosingContexts {};
    closing.markClosing(grazerId, "2", closedAtMs);
    closing.removeClosingContexts(grazerId, makeTabs({"1"}), closedAtMs + 10);

    check(closing.removeClosingContexts(grazerId, makeTabs({"1", "2"}), closedAtMs + 20).size() == 2);
};

auto tRefusedCloseShowsAgain = test("ClosingContexts/a tab its app kept past the timeout shows again") = []
{
    auto closing = ClosingContexts {};
    closing.markClosing(grazerId, "2", closedAtMs);

    auto afterTimeout = closedAtMs + ClosingContexts::closeTimeoutMs + 1;

    check(closing.removeClosingContexts(grazerId, makeTabs({"1", "2"}), afterTimeout).size() == 2);
};
