#pragma once

#include <filesystem>
#include <string>
#include <vector>

// An app that changed its bundle id. Rudder's stores key apps by bundle id
// ("<id>", "<id>/<contextId>", learned picks "<query>\n<id>"), so a rename
// would otherwise lose that app's history.
struct RenamedAppId
{
    std::string oldId;
    std::string newId;
};

// Jamie's apps moved to audio.pond.<app> (his domain is pond.audio).
const std::vector<RenamedAppId>& getRenamedPondAppIds();

// Rewrites every id in the JSON text, as a whole key or value or as the part
// before "/" or after an escaped newline. Other ids that merely start the
// same way ("com.pond.rudderx") are left alone.
std::string replaceRenamedAppIds(const std::string& jsonText,
                                 const std::vector<RenamedAppId>& renames);

// Rewrites the file in place; true when it changed something. A rename is
// skipped when the file already mentions its new id, so data written since
// the rename never meets a duplicate key.
bool replaceRenamedAppIdsInFile(const std::filesystem::path& file,
                                const std::vector<RenamedAppId>& renames);

// Every .json file directly inside directory; none when it doesn't exist.
std::vector<std::filesystem::path> listJsonFiles(const std::filesystem::path& directory);

// The files that changed.
std::vector<std::filesystem::path>
    replaceRenamedAppIdsInFiles(const std::vector<std::filesystem::path>& files,
                                const std::vector<RenamedAppId>& renames);
