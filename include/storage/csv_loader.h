#pragma once

#include "storage/table.h"

#include <string>
#include <vector>

// Loads CSV-formatted table data into in-memory rows.
class CsvLoader {
public:
    static Table loadTable(const std::string& tablePath);
    static void loadIntoDatabase(Database& db, const std::string& tableName, const std::string& tablePath);

private:
    static void ParseTableFile(const std::string& tablePath, std::vector<std::string>& headers, Table& table);
    static std::vector<std::string> ParseLine(const std::string& line);
    static void RemoveTrailingCarriageReturn(std::string& line);
    static void RemoveUtf8Bom(std::string& text);
};
