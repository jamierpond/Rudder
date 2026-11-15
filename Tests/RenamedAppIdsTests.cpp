#include <RenamedAppIds.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace nano;

namespace
{
const auto cowTermRename = std::vector<RenamedAppId> {{"com.eacp.cowterm", "audio.pond.cowterm"}};

std::filesystem::path writeFreshStoreFile(std::string_view name, const std::string& text)
{
    auto directory = std::filesystem::temp_directory_path() / "RenamedAppIdsTests";
    std::filesystem::create_directories(directory);
    auto file = directory / name;
    std::ofstream {file} << text;
    return file;
}

std::string readStoreFile(const std::filesystem::path& file)
{
    auto stream = std::ifstream {file};
    return {std::istreambuf_iterator<char> {stream}, {}};
}
} // namespace

auto tRenamesKeysAndValues = test("RenamedAppIds/an app id, its contexts and learned picks move")
    = []
{
    const auto before = R"({"com.eacp.cowterm": 1, "com.eacp.cowterm/branch:main": 2,)"
                        R"( "cowt\ncom.eacp.cowterm": {"destinationKey": "com.eacp.cowterm"}})";
    const auto after = R"({"audio.pond.cowterm": 1, "audio.pond.cowterm/branch:main": 2,)"
                       R"( "cowt\naudio.pond.cowterm": {"destinationKey": "audio.pond.cowterm"}})";

    check(replaceRenamedAppIds(before, cowTermRename) == after);
};

auto tLeavesLookalikes = test("RenamedAppIds/ids that only start the same stay") = []
{
    const auto text = R"({"com.eacp.cowtermx": 1, "x.com.eacp.cowterm": 2, "com.eacp.cowterm.daemon": 3})";
    check(replaceRenamedAppIds(text, cowTermRename) == text);
};

auto tFileRewritten = test("RenamedAppIds/a store file is rewritten once") = []
{
    auto file = writeFreshStoreFile("once.json", R"({"com.eacp.cowterm/pane:1": 1})");

    check(replaceRenamedAppIdsInFile(file, cowTermRename));
    check(readStoreFile(file) == R"({"audio.pond.cowterm/pane:1": 1})");
    check(!replaceRenamedAppIdsInFile(file, cowTermRename));
};

auto tSkipsWhenNewIdPresent
    = test("RenamedAppIds/a file already holding the new id is left alone") = []
{
    const auto text = std::string {R"({"com.eacp.cowterm/a": 1, "audio.pond.cowterm/a": 2})"};
    auto file = writeFreshStoreFile("both.json", text);

    check(!replaceRenamedAppIdsInFile(file, cowTermRename));
    check(readStoreFile(file) == text);
};
