#include "storage/csv_loader.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// CSV loader used by main to bootstrap tables from files.

Table CsvLoader::loadTable(const std::string& csvPath) {
    std::ifstream file(csvPath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open CSV file: " + csvPath);
    }

    std::string headerLine;
    size_t lineNumber = 0;
    while (std::getline(file, headerLine)) {
        ++lineNumber;
        RemoveTrailingCarriageReturn(headerLine);
        if (!headerLine.empty()) {
            break;
        }
    }

    if (headerLine.empty()) {
        throw std::runtime_error("CSV file is empty: " + csvPath);
    }

    std::vector<std::string> headers = ParseLine(headerLine);
    if (headers.empty()) {
        throw std::runtime_error("CSV header row is empty in file: " + csvPath);
    }

    RemoveUtf8Bom(headers[0]);

    Table table;

    std::string line;
    while (std::getline(file, line)) {
        ++lineNumber;
        RemoveTrailingCarriageReturn(line);

        if (line.empty()) {
            continue;
        }

        std::vector<std::string> values = ParseLine(line);
        if (values.size() != headers.size()) {
            throw std::runtime_error(
                "CSV column count mismatch at line " + std::to_string(lineNumber) +
                ": expected " + std::to_string(headers.size()) +
                ", got " + std::to_string(values.size()));
        }

        Row row;
        row.reserve(headers.size());
        for (size_t i = 0; i < headers.size(); ++i) {
            row[headers[i]] = std::move(values[i]);
        }

        table.push_back(std::move(row));
    }

    return table;
}

void CsvLoader::loadIntoDatabase(Database& db, const std::string& tableName, const std::string& csvPath) {
    db.tables[tableName] = loadTable(csvPath);
}

std::vector<std::string> CsvLoader::ParseLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (c == '"') {
            // Escaped quote inside quoted field is represented as "".
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                field += '"';
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field += c;
        }
    }

    if (inQuotes) {
        throw std::runtime_error("CSV parse error: unmatched quote in line: " + line);
    }

    fields.push_back(std::move(field));
    return fields;
}

void CsvLoader::RemoveTrailingCarriageReturn(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

void CsvLoader::RemoveUtf8Bom(std::string& text) {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
}
