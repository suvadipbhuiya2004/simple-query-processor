#pragma once
#include "storage/table.hpp"
#include <string>
#include <vector>

// reads and writes CSV files that back in-memory Tables
class CsvLoader {
private:
    // Internal helpers
    static void parseFile(const std::string &filePath, std::vector<std::string>& outHeaders, Table &outTable);

    static std::vector<std::string> parseLine(const std::string& line);
    static std::string escapeField(const std::string& field);

    static void stripTrailingCR(std::string& line) noexcept;
    static void stripUtf8Bom(std::string& text) noexcept;
    
public:
    // Load a CSV file into a bare Table
    static Table loadTable(const std::string& filePath);

    // Load a CSV file and register the result in db under tableName
    static void loadIntoDatabase(Database &db, const std::string& tableName, const std::string& filePath);

    // Persist an in-memory Table back to disk
    static void saveTable(const std::string &filePath, const std::vector<std::string>& headers, const Table &table);
};