#include "Renames.h"

namespace rudder
{
RenameStore::RenameStore(const emberstore::Database& db)
    : names(db.collection<Rename>("names"))
{
}

std::string RenameStore::nameFor(const std::string& key)
{
    if (auto rename = names.get(key))
        return rename->name;

    return {};
}

void RenameStore::forget(const std::string& key)
{
    names.remove(key);
}

void RenameStore::set(const std::string& key, const std::string& name)
{
    const auto* spaces = " \t\r\n";
    auto first = name.find_first_not_of(spaces);

    if (first == std::string::npos)
    {
        names.remove(key);
        return;
    }

    auto last = name.find_last_not_of(spaces);
    names.doc(key).set({name.substr(first, last - first + 1)});
}
} // namespace rudder
