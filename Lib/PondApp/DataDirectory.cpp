#include "DataDirectory.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/StdPath.h>

namespace pondapp
{
namespace
{
constexpr auto pondFolderName = std::string_view {"pond"};
} // namespace

std::filesystem::path getPondAppDataDirectory(std::string_view appName)
{
    return eacp::toStdPath(eacp::FilePath::appSupportDirectory(pondFolderName, appName));
}

std::optional<DataDirectoryMove>
    moveOldDataDirectory(const std::filesystem::path& oldDirectory,
                         const std::filesystem::path& newDirectory)
{
    auto error = std::error_code {};

    if (!std::filesystem::is_directory(oldDirectory, error)
        || std::filesystem::exists(newDirectory, error))
        return std::nullopt;

    auto move = DataDirectoryMove {.from = oldDirectory, .to = newDirectory};

    std::filesystem::create_directories(newDirectory.parent_path(), error);
    std::filesystem::rename(oldDirectory, newDirectory, error);

    if (error)
        move.error = error.message();

    return move;
}
} // namespace pondapp
