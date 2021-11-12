#include "nametbl.h"

#include <stdexcept>

NameTable::NameTable(const NameTable& src) {
    name_to_id_ = src.name_to_id_;
    std::map<std::string, int>::const_iterator it = name_to_id_.begin();
    while (it != name_to_id_.end()) {
        id_to_name_.insert(std::pair<int, const std::string*>(it->second, &it->first));
        it++;
    }
}

NameTable& NameTable::operator=(const NameTable& src) {
    if (this != &src) {
        name_to_id_ = src.name_to_id_;
        id_to_name_.clear();
        std::map<std::string, int>::const_iterator it = name_to_id_.begin();
        while (it != name_to_id_.end()) {
            id_to_name_.insert(std::pair<int, const std::string*>(it->second, &it->first));
            it++;
        }
    }
    return *this;
}

void NameTable::clear() {
    name_to_id_.clear();
    id_to_name_.clear();
}

bool NameTable::insertName(const std::string& name, int& id) {
    std::pair<std::map<std::string, int>::iterator, bool> ins_res = name_to_id_.insert(
        std::pair<std::string, int>(name, id));
    if (ins_res.second) {
        std::pair<std::map<int, const std::string*>::iterator, bool> ins_res2 = id_to_name_.insert(
            std::pair<int, const std::string*>(id, &ins_res.first->first));
        if (!ins_res2.second) throw std::logic_error("already used identifier");
        return true;
    }
    id = ins_res.first->second;
    return false;
}

bool NameTable::findName(const std::string& name, int& id) const {
    std::map<std::string, int>::const_iterator it = name_to_id_.find(name);
    if (it != name_to_id_.end()) {
        id = it->second;
        return true;
    }
    return false;
}

bool NameTable::getIdName(int id, std::string& name) const {
    std::map<int, const std::string*>::const_iterator it = id_to_name_.find(id);
    if (it != id_to_name_.end()) {
        name = *(it->second);
        return true;
    }
    name = "????";
    return false;
}
