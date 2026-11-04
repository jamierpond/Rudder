#include "PanelDump.h"

#include "Ranking.h"

#include <Miro/Reflection/Serialize.h>

#include <fstream>
#include <utility>

namespace rudder
{
namespace
{
struct DumpedContext
{
    std::string id;
    std::string title;
    std::string subtitle;
    std::string url;
    std::string path;
    std::string repo;
    std::string branch;
    bool open = true;
    bool focused = false;
    std::string key;

    MIRO_REFLECT(id, title, subtitle, url, path, repo, branch, open, focused, key)
};

struct DumpedRow
{
    std::string id;
    std::string kind;
    std::string appId;
    std::string contextId;
    std::string title;
    std::string subtitle;
    bool running = true;
    std::string key;
    std::int64_t lastUsed = 0;

    MIRO_REFLECT(id, kind, appId, contextId, title, subtitle, running, key, lastUsed)
};

struct PushRecord
{
    std::string event = "push";
    std::int64_t at = 0;
    std::string appId;
    std::vector<DumpedContext> contexts;

    MIRO_REFLECT(event, at, appId, contexts)
};

struct PanelRecord
{
    std::string event = "panel";
    std::int64_t at = 0;
    std::string reason;
    std::string query;
    std::string frontAppId;
    int selected = 0;
    std::vector<DumpedRow> rows;

    MIRO_REFLECT(event, at, reason, query, frontAppId, selected, rows)
};

struct SummonRecord
{
    std::string event = "panel";
    std::int64_t at = 0;
    std::string reason;
    std::string query;
    std::string frontAppId;
    int selected = 0;
    std::vector<DumpedRow> rows;
    std::map<std::string, std::vector<DumpedContext>> providers;

    MIRO_REFLECT(event, at, reason, query, frontAppId, selected, rows, providers)
};

const std::map<PanelDumpReason, std::string> reasonNames {
    {PanelDumpReason::summon, "summon"},
    {PanelDumpReason::query, "query"},
    {PanelDumpReason::refresh, "refresh"},
};

std::vector<DumpedContext> dumpContexts(const std::string& appId,
                                        const std::vector<RudderContext>& contexts)
{
    auto dumped = std::vector<DumpedContext> {};

    for (auto& context: contexts)
    {
        auto entry = DumpedContext {};
        entry.id = context.id;
        entry.title = context.title;
        entry.subtitle = context.subtitle;
        entry.url = context.url;
        entry.path = context.path;
        entry.repo = context.repo;
        entry.branch = context.branch;
        entry.open = context.open;
        entry.focused = context.focused;
        entry.key = contextKey(appId, context);
        dumped.push_back(std::move(entry));
    }

    return dumped;
}

std::vector<DumpedRow>
    dumpRows(const std::vector<RudderItem>& rows,
             const std::function<std::int64_t(const std::string&)>& getLastUsed)
{
    auto dumped = std::vector<DumpedRow> {};

    for (auto& row: rows)
    {
        auto entry = DumpedRow {};
        entry.id = row.id;
        entry.kind = row.kind;
        entry.appId = row.appId;
        entry.contextId = row.contextId;
        entry.title = row.title;
        entry.subtitle = row.subtitle;
        entry.running = row.running;
        entry.key = frecencyKey(row);
        entry.lastUsed = getLastUsed(entry.key);
        dumped.push_back(std::move(entry));
    }

    return dumped;
}
} // namespace

PanelDump::PanelDump(std::optional<std::filesystem::path> directory)
{
    if (!directory)
        return;

    auto error = std::error_code {};
    std::filesystem::create_directories(*directory, error);
    file = *directory / "panel.jsonl";
}

bool PanelDump::isEnabled() const
{
    return file.has_value();
}

void PanelDump::writePush(std::int64_t atMs,
                          const std::string& appId,
                          const std::vector<RudderContext>& contexts)
{
    if (!file)
        return;

    auto record = PushRecord {};
    record.at = atMs;
    record.appId = appId;
    record.contexts = dumpContexts(appId, contexts);
    appendLine(Miro::toJSONString(record));
}

void PanelDump::writePanel(
    std::int64_t atMs,
    PanelDumpReason reason,
    const std::string& query,
    const std::string& frontAppId,
    int selected,
    const std::vector<RudderItem>& rows,
    const std::function<std::int64_t(const std::string&)>& getLastUsed,
    const std::map<std::string, std::vector<RudderContext>>& providerContexts)
{
    if (!file)
        return;

    if (reason != PanelDumpReason::summon)
    {
        auto record = PanelRecord {};
        record.at = atMs;
        record.reason = reasonNames.at(reason);
        record.query = query;
        record.frontAppId = frontAppId;
        record.selected = selected;
        record.rows = dumpRows(rows, getLastUsed);
        appendLine(Miro::toJSONString(record));
        return;
    }

    auto record = SummonRecord {};
    record.at = atMs;
    record.reason = reasonNames.at(reason);
    record.query = query;
    record.frontAppId = frontAppId;
    record.selected = selected;
    record.rows = dumpRows(rows, getLastUsed);

    for (auto& [appId, contexts]: providerContexts)
        record.providers[appId] = dumpContexts(appId, contexts);

    appendLine(Miro::toJSONString(record));
}

void PanelDump::appendLine(const std::string& line)
{
    auto stream = std::ofstream {*file, std::ios::app};
    stream << line << '\n';
}
} // namespace rudder
