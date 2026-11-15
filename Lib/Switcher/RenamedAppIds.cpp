#include "RenamedAppIds.h"

#include <fstream>
#include <iterator>
#include <regex>

namespace
{
std::string escapeForRegex(const std::string& text)
{
    static const auto special = std::regex {R"([.^$|()\[\]{}*+?\\])"};
    return std::regex_replace(text, special, R"(\$&)");
}

std::string readWholeFile(const std::filesystem::path& file)
{
    auto stream = std::ifstream {file, std::ios::binary};
    return {std::istreambuf_iterator<char> {stream}, {}};
}

// A JSON string opens with a quote; a learned pick's id follows "\n".
std::regex makeIdPattern(const std::string& id)
{
    return std::regex {R"((\"|\\n))" + escapeForRegex(id) + R"((?=[\"/]))"};
}

std::string replaceOneRenamedAppId(const std::string& jsonText, const RenamedAppId& rename)
{
    return std::regex_replace(jsonText, makeIdPattern(rename.oldId), "$1" + rename.newId);
}
} // namespace

const std::vector<RenamedAppId>& getRenamedPondAppIds()
{
    static const auto renames = std::vector<RenamedAppId> {
        {"com.pond.rudder", "audio.pond.rudder"},
        {"com.eacp.cowterm", "audio.pond.cowterm"},
        {"com.pond.diffview", "audio.pond.diffview"},
        {"com.pond.screencap", "audio.pond.screencap"},
    };
    return renames;
}

std::string replaceRenamedAppIds(const std::string& jsonText,
                                 const std::vector<RenamedAppId>& renames)
{
    auto result = jsonText;

    for (const auto& rename: renames)
        result = replaceOneRenamedAppId(result, rename);

    return result;
}

bool replaceRenamedAppIdsInFile(const std::filesystem::path& file,
                                const std::vector<RenamedAppId>& renames)
{
    if (!std::filesystem::exists(file))
        return false;

    const auto before = readWholeFile(file);
    auto after = before;

    for (const auto& rename: renames)
    {
        if (!std::regex_search(after, makeIdPattern(rename.newId)))
            after = replaceOneRenamedAppId(after, rename);
    }

    if (after == before)
        return false;

    const auto temporary = std::filesystem::path {file.string() + ".renaming"};
    std::ofstream {temporary, std::ios::binary} << after;

    auto error = std::error_code {};
    std::filesystem::rename(temporary, file, error);
    return !error;
}

std::vector<std::filesystem::path> listJsonFiles(const std::filesystem::path& directory)
{
    auto files = std::vector<std::filesystem::path> {};
    auto error = std::error_code {};

    for (const auto& entry: std::filesystem::directory_iterator {directory, error})
    {
        if (entry.path().extension() == ".json")
            files.push_back(entry.path());
    }

    return files;
}

std::vector<std::filesystem::path>
    replaceRenamedAppIdsInFiles(const std::vector<std::filesystem::path>& files,
                                const std::vector<RenamedAppId>& renames)
{
    auto changed = std::vector<std::filesystem::path> {};

    for (const auto& file: files)
    {
        if (replaceRenamedAppIdsInFile(file, renames))
            changed.push_back(file);
    }

    return changed;
}
