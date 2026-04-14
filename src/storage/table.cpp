#include "storage/table.hpp"
#include <algorithm>
#include <stdexcept>

bool Database::hasTable(const std::string &name) const noexcept {
    return tables.count(name) != 0;
}

Table &Database::getTable(const std::string &name) {
    const auto it = tables.find(name);
    if (it == tables.end()) {
        throw std::runtime_error("Table not found: " + name);
    }
    return it->second;
}

const Table &Database::getTable(const std::string &name) const {
    const auto it = tables.find(name);
    if (it == tables.end()) {
        throw std::runtime_error("Table not found: " + name);
    }
    return it->second;
}

bool Database::hasSchema(const std::string &name) const noexcept {
    return schemas.count(name) != 0;
}

const std::vector<std::string> &Database::getSchema(const std::string &name) const {
    const auto it = schemas.find(name);
    if (it == schemas.end()) {
        throw std::runtime_error("Schema not found for table: " + name);
    }
    return it->second;
}

void Database::markTableMutated(const std::string &name) {
    ++tableVersions_[name];
    hashIndexCache_.erase(name);
}

std::size_t Database::tableVersion(const std::string &name) const noexcept {
    const auto it = tableVersions_.find(name);
    return (it == tableVersions_.end()) ? 0U : it->second;
}

const std::unordered_map<std::string, std::vector<std::size_t>> &
Database::getOrBuildHashIndex(const std::string &tableName, const std::string &columnName) const {
    const auto tableIt = tables.find(tableName);
    if (tableIt == tables.end()) {
        throw std::runtime_error("Table not found: " + tableName);
    }

    const Table &table = tableIt->second;
    const std::size_t currentVersion = tableVersion(tableName);

    auto &tableCache = hashIndexCache_[tableName];
    auto &entry = tableCache[columnName];
    if (entry.version == currentVersion && !entry.buckets.empty()) {
        return entry.buckets;
    }

    entry.buckets.clear();
    entry.buckets.reserve(table.size() * 2U + 1U);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto it = table[i].find(columnName);
        const std::string key = (it == table[i].end()) ? std::string() : it->second;
        entry.buckets[key].push_back(i);
    }
    entry.version = currentVersion;
    return entry.buckets;
}