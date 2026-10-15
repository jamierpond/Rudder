#include "TestSupport.h"

#include <Renames.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace rudder;

auto tRenameStored = test("Renames/a name is stored by key, trimmed, and survives a reopen")
    = []
{
    auto db = testing::freshDatabase("renames-store");
    auto key = std::string {"com.eacp.cowterm/branch:fix-login"};

    {
        auto renames = RenameStore {db};
        check(renames.nameFor(key).empty());

        renames.set(key, "  Login bug ");
        check(renames.nameFor(key) == "Login bug");
    }

    auto again = RenameStore {db};
    check(again.nameFor(key) == "Login bug");
    check(again.nameFor("com.eacp.cowterm/branch:other").empty());
};

auto tEmptyNameClears = test("Renames/an empty name clears the rename") = []
{
    auto db = testing::freshDatabase("renames-clear");
    auto renames = RenameStore {db};
    auto key = std::string {"com.eacp.cowterm/path:/p/notes"};

    renames.set(key, "Notes");
    renames.set(key, "   ");

    check(renames.nameFor(key).empty());
    check(renames.names.size() == 0);
};
