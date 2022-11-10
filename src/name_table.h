#pragma once

#include <map>
#include <optional>
#include <string>

class NameTable {
 public:
    NameTable() = default;
    ~NameTable() = default;
    NameTable(const NameTable&) = delete;
    NameTable& operator=(const NameTable&) = delete;

    void clear();
    std::pair<unsigned, bool> insertName(std::string name, unsigned id);
    std::optional<unsigned> findName(std::string_view name) const;
    std::string_view getName(unsigned id) const;

 private:
    std::map<std::string, unsigned, std::less<>> name_to_id_;
    std::map<unsigned, std::string_view> id_to_name_;
};
