#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pondapp
{
// What main returns when it handed its launch to the copy already running.
constexpr auto exitCodeAnotherInstanceIsRunning = 3;

// One running copy of an app per user. The first launch claims a named eacp IPC
// channel; the kernel releases the claim when that process ends, crashed or not.
// A later launch dials the holder, hands it its arguments and should exit with
// exitCodeAnotherInstanceIsRunning. Construct on the main thread, before or
// inside the event loop, and keep it alive for the life of the app.
class SingleInstance
{
public:
    // instanceName is short and unique per app, e.g. "audio.pond.grazer".
    explicit SingleInstance(std::string instanceName);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool isTheRunningInstance() const;

    // Asks the running copy to come forward with arguments. True once it
    // answered: exit then. False when nobody answered (the holder is quitting);
    // this launch then waits briefly to take the claim and should carry on
    // opening, since a second window beats an app that refuses to open.
    bool handOverToRunningInstance(const std::vector<std::string>& arguments);

    // Runs on the main thread of the running copy for each later launch, with
    // that launch's arguments. Launches that arrived before this was set are
    // delivered now, in order.
    void setSecondLaunchHandler(
        std::function<void(const std::vector<std::string>&)> handler);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// argv without the program name, as handOverToRunningInstance takes it.
std::vector<std::string> getLaunchArguments(int argc, char** argv);
} // namespace pondapp
