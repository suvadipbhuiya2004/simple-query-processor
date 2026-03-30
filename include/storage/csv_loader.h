#pragma once

#include "storage/table.h"

#include <string>
#include <vector>

// Loads CSV data into in-memory table rows.
class CsvLoader {
public:
    static Table loadTable(const std::string& csvPath);
    static void loadIntoDatabase(Database& db, const std::string& tableName, const std::string& csvPath);

private:
    static std::vector<std::string> ParseLine(const std::string& line);
    static void RemoveTrailingCarriageReturn(std::string& line);
    static void RemoveUtf8Bom(std::string& text);
};
