#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


struct ColumnMetadata {
    std::string name;
    std::string type;
    bool primaryKey = false;
    bool unique = false;
    bool notNull = false;
    std::optional<std::string> foreignKey;
    std::optional<std::string> checkExpr;
};

struct TableMetadata {
    std::string file;
    std::vector<ColumnMetadata> columns;
    std::vector<std::string> tableChecks;
};


// Owns a single metadata.json file under data Directory
class MetadataCatalog {
private:
    std::string dataDir_;
    std::unordered_map<std::string, TableMetadata> tables_;
    
public:
    explicit MetadataCatalog(std::string dataDirectory);

    // Returns true if metadata.json exists
    [[nodiscard]] bool metadataFileExists() const;

    // Load metadata.json into memory
    void load();

    // Flush current in-memory state to metadata.json
    void save() const;

    // Table existence / retrieval
    [[nodiscard]] bool hasTable(const std::string& name) const noexcept;
    [[nodiscard]] const TableMetadata& getTable(const std::string& name) const;

    // Ordered list of column names for a table
    [[nodiscard]] std::vector<std::string> getColumnOrder(const std::string& name) const;

    // Upsert / remove
    void upsertTable(const std::string& name, TableMetadata metadata);
    void removeTable(const std::string& name);

    // Iteration
    [[nodiscard]] const std::unordered_map<std::string, TableMetadata>& allTables() const noexcept;

    // Path helpers
    [[nodiscard]] const std::string& dataDirectory() const noexcept;
    [[nodiscard]] std::string metadataFilePath() const;
    [[nodiscard]] std::string tableFilePath(const std::string& tableName) const;
};