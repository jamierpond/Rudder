#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace pondapp
{
// Where a pond app keeps its data: ~/Library/Application Support/pond/<appName>
// on macOS, %APPDATA%\pond\<appName> on Windows. Not created here.
std::filesystem::path getPondAppDataDirectory(std::string_view appName);

struct DataDirectoryMove
{
    std::filesystem::path from;
    std::filesystem::path to;
    // Empty when the rename worked.
    std::string error;
};

// Renames oldDirectory to newDirectory in one step, but only when oldDirectory
// exists and newDirectory does not: data already in the new place is never
// touched, and nothing is ever deleted. Nothing when there was nothing to move.
std::optional<DataDirectoryMove>
    moveOldDataDirectory(const std::filesystem::path& oldDirectory,
                         const std::filesystem::path& newDirectory);
} // namespace pondapp
