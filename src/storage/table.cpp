#include "storage/table.hpp"
#include <stdexcept>

bool Database::hasTable(const std::string& name) const noexcept {
    return tables.count(name) != 0;
}

Table& Database::getTable(const std::string& name) {
    const auto it = tables.find(name);
    if (it == tables.end()) {
        throw std::runtime_error("Table not found: " + name);
    }
    return it->second;
}

const Table& Database::getTable(const std::string& name) const {
    const auto it = tables.find(name);
    if (it == tables.end()) {
        throw std::runtime_error("Table not found: " + name);
    }
    return it->second;
}

bool Database::hasSchema(const std::string& name) const noexcept {
    return schemas.count(name) != 0;
}

const std::vector<std::string>& Database::getSchema(const std::string& name) const {
    const auto it = schemas.find(name);
    if (it == schemas.end()) {
        throw std::runtime_error("Schema not found for table: " + name);
    }
    return it->second;
}