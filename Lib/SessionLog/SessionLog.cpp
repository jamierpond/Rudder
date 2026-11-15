#include "SessionLog.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace sessionlog
{
namespace
{
constexpr auto sessionLogExtension = std::string_view {".log"};

std::string_view getSessionStartText(const std::string& fileName)
{
    if (!fileName.ends_with(sessionLogExtension))
        return {};

    return std::string_view {fileName}.substr(
        0, fileName.size() - sessionLogExtension.size());
}

bool isSessionLogName(const std::string& fileName)
{
    auto startText = getSessionStartText(fileName);
    return !startText.empty()
           && std::all_of(startText.begin(),
                          startText.end(),
                          [](char character)
                          { return character >= '0' && character <= '9'; });
}

// Equal-length digit strings sort like numbers; a shorter one is an older time.
bool isOlderSessionLog(const std::string& left, const std::string& right)
{
    if (left.size() != right.size())
        return left.size() < right.size();

    return left < right;
}
} // namespace

std::string getSessionLogPath(const std::string& logsDirectory,
                              std::int64_t sessionStartMs)
{
    auto fileName =
        std::to_string(sessionStartMs) + std::string {sessionLogExtension};
    return (std::filesystem::path {logsDirectory} / fileName).string();
}

std::vector<std::string> findSessionLogsToDelete(std::vector<std::string> fileNames,
                                                 std::size_t sessionsToKeep)
{
    std::erase_if(fileNames,
                  [](const std::string& name) { return !isSessionLogName(name); });
    std::sort(fileNames.begin(), fileNames.end(), isOlderSessionLog);

    if (fileNames.size() <= sessionsToKeep)
        return {};

    fileNames.resize(fileNames.size() - sessionsToKeep);
    return fileNames;
}

void deleteOldSessionLogs(const std::string& logsDirectory,
                          std::size_t sessionsToKeep)
{
    auto error = std::error_code {};
    auto fileNames = std::vector<std::string> {};

    for (auto& entry: std::filesystem::directory_iterator {logsDirectory, error})
        fileNames.push_back(entry.path().filename().string());

    for (auto& name: findSessionLogsToDelete(std::move(fileNames), sessionsToKeep))
        std::filesystem::remove(std::filesystem::path {logsDirectory} / name, error);
}
} // namespace sessionlog
