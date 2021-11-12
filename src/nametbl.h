#pragma once

#include <map>
#include <string>

class NameTable {
 public:
    NameTable() = default;
    ~NameTable() = default;
    NameTable(const NameTable&);

    void clear();
    bool insertName(const std::string&, int&);
    bool findName(const std::string&, int&) const;
    bool getIdName(int, std::string&) const;

    NameTable& operator=(const NameTable&);

 private:
    std::map<std::string, int> name_to_id_;
    std::map<int, const std::string*> id_to_name_;
};
