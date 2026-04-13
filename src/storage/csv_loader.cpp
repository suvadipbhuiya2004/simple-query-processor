#include "storage/csv_loader.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace
{

    // Parse one RFC-4180-compliant CSV line.  Handles:
    //   • Quoted fields containing commas, newlines, and escaped double-quotes ("").
    //   • Unquoted fields (no special characters).
    std::vector<std::string> parseCsvLine(const std::string &line)
    {
        std::vector<std::string> fields;
        std::string field;
        bool inQuotes = false;

        for (std::size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];

            if (c == '"')
            {
                if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
                {
                    field += '"';
                    ++i; // skip second quote of escape pair
                }
                else
                {
                    inQuotes = !inQuotes;
                }
            }
            else if (c == ',' && !inQuotes)
            {
                fields.push_back(std::move(field));
                field.clear();
            }
            else
            {
                field += c;
            }
        }

        if (inQuotes)
        {
            throw std::runtime_error("CSV parse error: unterminated quote in: " + line);
        }
        fields.push_back(std::move(field));
        return fields;
    }

    // Wrap a field in double-quotes if it contains , " \n or \r.
    std::string escapeCsvField(const std::string &f)
    {
        if (f.find_first_of(",\"\n\r") == std::string::npos)
            return f;

        std::string out;
        out.reserve(f.size() + 2);
        out += '"';
        for (const char c : f)
        {
            if (c == '"')
                out += "\"\"";
            else
                out += c;
        }
        out += '"';
        return out;
    }

    void stripTrailingCR(std::string &s) noexcept
    {
        if (!s.empty() && s.back() == '\r')
            s.pop_back();
    }

    // Strip UTF-8 BOM (EF BB BF) from the first header token if present.
    void stripUtf8Bom(std::string &s) noexcept
    {
        if (s.size() >= 3 &&
            static_cast<unsigned char>(s[0]) == 0xEFu &&
            static_cast<unsigned char>(s[1]) == 0xBBu &&
            static_cast<unsigned char>(s[2]) == 0xBFu)
        {
            s.erase(0, 3);
        }
    }

} // namespace

// ---------------------------------------------------------------------------
// CsvLoader — private core
// ---------------------------------------------------------------------------

void CsvLoader::parseFile(const std::string &filePath,
                          std::vector<std::string> &outHeaders,
                          Table &outTable)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open CSV file: " + filePath);
    }

    // --- Header row ---
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(file, line))
    {
        ++lineNumber;
        stripTrailingCR(line);
        if (!line.empty())
            break;
    }
    if (line.empty())
    {
        throw std::runtime_error("CSV file is empty: " + filePath);
    }

    outHeaders = parseCsvLine(line);
    if (outHeaders.empty())
    {
        throw std::runtime_error("Header row is empty in: " + filePath);
    }
    stripUtf8Bom(outHeaders[0]);

    // --- Data rows ---
    outTable.clear();
    while (std::getline(file, line))
    {
        ++lineNumber;
        stripTrailingCR(line);
        if (line.empty())
            continue;

        auto values = parseCsvLine(line);
        if (values.size() != outHeaders.size())
        {
            throw std::runtime_error(
                "Column count mismatch at line " + std::to_string(lineNumber) +
                " in " + filePath +
                ": expected " + std::to_string(outHeaders.size()) +
                ", got " + std::to_string(values.size()));
        }

        Row row;
        row.reserve(outHeaders.size());
        for (std::size_t i = 0; i < outHeaders.size(); ++i)
        {
            row.emplace(outHeaders[i], std::move(values[i]));
        }
        outTable.push_back(std::move(row));
    }
}

// ---------------------------------------------------------------------------
// CsvLoader — public API
// ---------------------------------------------------------------------------

Table CsvLoader::loadTable(const std::string &filePath)
{
    std::vector<std::string> headers;
    Table table;
    parseFile(filePath, headers, table);
    return table;
}

void CsvLoader::loadIntoDatabase(Database &db,
                                 const std::string &tableName,
                                 const std::string &filePath)
{
    std::vector<std::string> headers;
    Table table;
    parseFile(filePath, headers, table);
    db.schemas[tableName] = std::move(headers);
    db.tables[tableName] = std::move(table);
}

void CsvLoader::saveTable(const std::string &filePath,
                          const std::vector<std::string> &headers,
                          const Table &table)
{
    if (headers.empty())
    {
        throw std::runtime_error("Cannot save table with empty headers: " + filePath);
    }

    std::ofstream file(filePath, std::ios::trunc);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open CSV file for writing: " + filePath);
    }

    // Header row
    for (std::size_t i = 0; i < headers.size(); ++i)
    {
        if (i > 0)
            file << ',';
        file << escapeCsvField(headers[i]);
    }
    file << '\n';

    // Data rows — write in schema order so output is deterministic.
    for (const auto &row : table)
    {
        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            if (i > 0)
                file << ',';
            const auto it = row.find(headers[i]);
            file << escapeCsvField(it != row.end() ? it->second : std::string{});
        }
        file << '\n';
    }

    if (!file)
    {
        throw std::runtime_error("Write error on CSV file: " + filePath);
    }
}

// Expose private helpers via the CsvLoader:: scope so the header declaration
// is satisfied (they were declared private but defined here as file-static
// wrappers that forward to the anonymous-namespace implementations).
void CsvLoader::stripTrailingCR(std::string &s) noexcept { ::stripTrailingCR(s); }
void CsvLoader::stripUtf8Bom(std::string &s) noexcept { ::stripUtf8Bom(s); }

std::vector<std::string> CsvLoader::parseLine(const std::string &line)
{
    return parseCsvLine(line);
}

std::string CsvLoader::escapeField(const std::string &field)
{
    return escapeCsvField(field);
}