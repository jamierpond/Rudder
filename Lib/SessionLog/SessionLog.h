#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sessionlog
{
// Each launch logs to its own file, named by when the session started
// (unix epoch milliseconds): <logsDirectory>/<sessionStartMs>.log.
std::string getSessionLogPath(const std::string& logsDirectory,
                              std::int64_t sessionStartMs);

// Of `fileNames`, the session logs to delete so only the newest
// `sessionsToKeep` remain. Names that aren't <digits>.log are never chosen.
std::vector<std::string> findSessionLogsToDelete(std::vector<std::string> fileNames,
                                                 std::size_t sessionsToKeep);

// Deletes the session logs in `logsDirectory` beyond the newest `sessionsToKeep`.
void deleteOldSessionLogs(const std::string& logsDirectory,
                          std::size_t sessionsToKeep);
} // namespace sessionlog
