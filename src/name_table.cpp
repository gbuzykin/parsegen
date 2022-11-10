#include "name_table.h"

#include <stdexcept>

void NameTable::clear() {
    name_to_id_.clear();
    id_to_name_.clear();
}

std::pair<unsigned, bool> NameTable::insertName(std::string name, unsigned id) {
    auto [it, success] = name_to_id_.emplace(std::move(name), id);
    if (!success) { return std::make_pair(it->second, false); }
    if (!id_to_name_.emplace(id, it->first).second) { throw std::logic_error("already used identifier"); }
    return std::make_pair(id, true);
}

std::optional<unsigned> NameTable::findName(std::string_view name) const {
    if (auto it = name_to_id_.find(name); it != name_to_id_.end()) { return it->second; }
    return {};
}

std::string_view NameTable::getName(unsigned id) const {
    if (auto it = id_to_name_.find(id); it != id_to_name_.end()) { return it->second; }
    return {};
}
