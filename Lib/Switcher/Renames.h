#pragma once

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <string>

namespace rudder
{
struct Rename
{
    std::string name;

    MIRO_REFLECT(name)
};

// The user's own names for contexts (a Claude session named badly by its
// provider), keyed by contextKey so a name outlives the pane it was given
// on and works for every provider. Rare, deliberate writes: give it a
// Durable database.
struct RenameStore
{
    explicit RenameStore(const emberstore::Database& db);

    // The name, or empty when the context isn't renamed.
    std::string nameFor(const std::string& key);

    // Sets the name, trimmed; an empty (or all-space) name clears it.
    void set(const std::string& key, const std::string& name);

    // Drops the name: its context is gone, and a context that turns up later
    // on the same key (a reused tab id, a new pane on the branch) starts
    // with the provider's own title.
    void forget(const std::string& key);

    emberstore::Collection<Rename> names;
};
} // namespace rudder
