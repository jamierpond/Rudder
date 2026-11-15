// PondAppTests is its own binary with its own main: the single-instance tests
// relaunch it as a holder and as a second launch, so two real processes race
// for the claim the way two app launches do.
#include <PondApp/DataDirectory.h>
#include <PondApp/SingleInstance.h>

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Time.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>

using namespace nano;

namespace
{
constexpr auto holdInstanceMode = std::string_view {"--hold-instance"};
constexpr auto launchAgainMode = std::string_view {"--launch-again"};
constexpr auto holderReadyLine = std::string_view {"holding"};
constexpr auto holderLifetime = eacp::Time::MS {10000};
constexpr auto holderPollInterval = eacp::Time::MS {20};
constexpr auto holderStartTimeout = std::chrono::seconds {10};
constexpr auto pollSleep = std::chrono::milliseconds {20};
constexpr auto exitCodeHandOverFailed = 1;
constexpr auto exitCodeClaimed = 0;

std::string executablePath;

std::filesystem::path makeFreshTestDirectory(std::string_view name)
{
    auto directory = std::filesystem::temp_directory_path() / "PondAppTests" / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

// A claim name no other test run uses, and where its holder writes what a
// second launch handed it.
struct TestInstance
{
    std::string name;
    std::filesystem::path receivedPath;
    std::filesystem::path readyPath;
};

TestInstance makeTestInstance(std::string_view testName)
{
    auto generator = std::random_device {};
    auto directory = makeFreshTestDirectory(testName);
    return {.name = "pond.test." + std::to_string(generator()),
            .receivedPath = directory / "received.txt",
            .readyPath = directory / "ready.txt"};
}

void writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream {path} << text;
}

std::string readTextFile(const std::filesystem::path& path)
{
    auto stream = std::ifstream {path};
    return {std::istreambuf_iterator<char> {stream}, {}};
}

// Holds the claim, writes each later launch's arguments to receivedPath (one
// line each, space-separated) and quits after the first one.
int runHolder(const TestInstance& testInstance)
{
    auto instance = pondapp::SingleInstance {testInstance.name};

    if (!instance.isTheRunningInstance())
        return exitCodeHandOverFailed;

    auto received = false;
    instance.setSecondLaunchHandler(
        [&](const std::vector<std::string>& arguments)
        {
            auto line = std::string {};

            for (const auto& argument: arguments)
                line += argument + " ";

            writeTextFile(testInstance.receivedPath, line);
            received = true;
        });

    writeTextFile(testInstance.readyPath, std::string {holderReadyLine});

    auto deadline = eacp::Time::Deadline {holderLifetime};

    while (!received && !deadline.expired())
        eacp::Threads::runEventLoopFor(holderPollInterval);

    return exitCodeClaimed;
}

int runSecondLaunch(const std::string& instanceName, std::vector<std::string> arguments)
{
    auto instance = pondapp::SingleInstance {instanceName};

    if (instance.isTheRunningInstance())
        return exitCodeClaimed;

    if (instance.handOverToRunningInstance(arguments))
        return pondapp::exitCodeAnotherInstanceIsRunning;

    return exitCodeHandOverFailed;
}

bool waitForFile(const std::filesystem::path& path)
{
    const auto giveUpAt = std::chrono::steady_clock::now() + holderStartTimeout;

    while (!std::filesystem::exists(path))
    {
        if (std::chrono::steady_clock::now() > giveUpAt)
            return false;

        std::this_thread::sleep_for(pollSleep);
    }

    return true;
}

eacp::Processes::Process startHolder(const TestInstance& testInstance)
{
    return eacp::Processes::Process {executablePath,
                                     {std::string {holdInstanceMode},
                                      testInstance.name,
                                      testInstance.receivedPath.string(),
                                      testInstance.readyPath.string()}};
}

eacp::Processes::ProcessResult launchAgain(const TestInstance& testInstance,
                                           eacp::Vector<std::string> arguments)
{
    auto allArguments =
        eacp::Vector<std::string> {std::string {launchAgainMode}, testInstance.name};

    for (auto& argument: arguments)
        allArguments.add(std::move(argument));

    return eacp::Processes::run(executablePath, allArguments);
}
} // namespace

auto tDataDirectoryIsUnderPond =
    test("PondApp/an app's data folder is pond/<App> under the app-data folder") = []
{
    auto directory = pondapp::getPondAppDataDirectory("Grazer");

    check(directory.filename() == "Grazer");
    check(directory.parent_path().filename() == "pond");
};

auto tOldDirectoryMovesWhenNewIsMissing =
    test("PondApp/an old data folder moves to the new place with its files") = []
{
    auto root = makeFreshTestDirectory("MoveOld");
    auto oldDirectory = root / "CowTerm";
    auto newDirectory = root / "pond" / "CowTerm";
    std::filesystem::create_directories(oldDirectory / "logs");
    writeTextFile(oldDirectory / "logs" / "1.log", "kept");

    auto move = pondapp::moveOldDataDirectory(oldDirectory, newDirectory);

    check(move.has_value());
    check(move->error.empty());
    check(!std::filesystem::exists(oldDirectory));
    check(readTextFile(newDirectory / "logs" / "1.log") == "kept");
};

auto tNewDirectoryIsNeverOverwritten =
    test("PondApp/an old data folder stays put when the new one already exists") = []
{
    auto root = makeFreshTestDirectory("KeepNew");
    auto oldDirectory = root / "DiffView";
    auto newDirectory = root / "pond" / "DiffView";
    std::filesystem::create_directories(oldDirectory);
    std::filesystem::create_directories(newDirectory);
    writeTextFile(oldDirectory / "old.txt", "old");

    check(!pondapp::moveOldDataDirectory(oldDirectory, newDirectory).has_value());
    check(readTextFile(oldDirectory / "old.txt") == "old");
};

auto tNothingToMove =
    test("PondApp/no old data folder means nothing moves") = []
{
    auto root = makeFreshTestDirectory("NothingOld");

    check(!pondapp::moveOldDataDirectory(root / "Missing", root / "pond" / "Missing")
               .has_value());
    check(!std::filesystem::exists(root / "pond"));
};

auto tFirstLaunchClaims =
    test("PondApp/the first launch is the running instance") = []
{
    auto result = launchAgain(makeTestInstance("FirstLaunch"), {});

    check(result.exitCode == exitCodeClaimed);
};

auto tSecondLaunchHandsOver =
    test("PondApp/a second launch hands its arguments to the running one and exits non-zero") = []
{
    auto testInstance = makeTestInstance("HandOver");
    auto holder = startHolder(testInstance);

    check(waitForFile(testInstance.readyPath));

    auto second = launchAgain(testInstance, {"https://example.com", "--new-tab"});

    check(second.exitCode == pondapp::exitCodeAnotherInstanceIsRunning);
    check(holder.wait() == exitCodeClaimed);
    check(readTextFile(testInstance.receivedPath) == "https://example.com --new-tab ");
};

auto tClaimFreedWhenHolderEnds =
    test("PondApp/after the running instance quits, the next launch claims") = []
{
    auto testInstance = makeTestInstance("Freed");

    {
        auto holder = startHolder(testInstance);
        check(waitForFile(testInstance.readyPath));
        holder.kill();
        holder.wait();
    }

    check(launchAgain(testInstance, {}).exitCode == exitCodeClaimed);
};

int main(int argc, char* argv[])
{
    executablePath = std::filesystem::absolute(argv[0]).string();

    if (argc == 5 && argv[1] == holdInstanceMode)
        return runHolder({.name = argv[2], .receivedPath = argv[3], .readyPath = argv[4]});

    if (argc >= 3 && argv[1] == launchAgainMode)
        return runSecondLaunch(argv[2], {argv + 3, argv + argc});

    return nano::run(argc, argv);
}
