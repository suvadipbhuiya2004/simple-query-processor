#include "storage/csv_loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

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

    std::vector<std::vector<std::string>> parseCsvBuffer(const std::string &buffer)
    {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> fields;
        std::string field;
        bool inQuotes = false;

        auto flushField = [&]()
        {
            fields.push_back(std::move(field));
            field.clear();
        };

        auto flushRow = [&]()
        {
            // Skip completely empty physical rows.
            if (fields.size() == 1 && fields.front().empty())
            {
                fields.clear();
                return;
            }
            rows.push_back(std::move(fields));
            fields.clear();
        };

        for (std::size_t i = 0; i < buffer.size(); ++i)
        {
            const char c = buffer[i];

            if (inQuotes)
            {
                if (c == '"')
                {
                    if (i + 1 < buffer.size() && buffer[i + 1] == '"')
                    {
                        field.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    field.push_back(c);
                }
                continue;
            }

            if (c == '"')
            {
                inQuotes = true;
                continue;
            }
            if (c == ',')
            {
                flushField();
                continue;
            }
            if (c == '\n')
            {
                flushField();
                flushRow();
                continue;
            }
            if (c == '\r')
            {
                continue;
            }
            field.push_back(c);
        }

        if (inQuotes)
        {
            throw std::runtime_error("CSV parse error: unterminated quote in input buffer");
        }

        if (!field.empty() || !fields.empty())
        {
            flushField();
            flushRow();
        }

        return rows;
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
        if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEFu &&
            static_cast<unsigned char>(s[1]) == 0xBBu && static_cast<unsigned char>(s[2]) == 0xBFu)
        {
            s.erase(0, 3);
        }
    }

} // namespace

// CsvLoader — private core
void CsvLoader::parseFile(const std::string &filePath, std::vector<std::string> &outHeaders, Table &outTable)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open CSV file: " + filePath);
    }

    std::string buffer;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size > 0)
    {
        buffer.resize(static_cast<std::size_t>(size));
        file.read(&buffer[0], static_cast<std::streamsize>(buffer.size()));
        if (!file && !file.eof())
        {
            throw std::runtime_error("Read error on CSV file: " + filePath);
        }
    }

    const auto rows = parseCsvBuffer(buffer);
    if (rows.empty())
    {
        throw std::runtime_error("CSV file is empty: " + filePath);
    }

    outHeaders = rows.front();
    if (outHeaders.empty())
    {
        throw std::runtime_error("Header row is empty in: " + filePath);
    }
    stripUtf8Bom(outHeaders[0]);

    outTable.clear();
    outTable.reserve(rows.size() > 1 ? rows.size() - 1 : 0);

    for (std::size_t rowIndex = 1; rowIndex < rows.size(); ++rowIndex)
    {
        const auto &values = rows[rowIndex];
        if (values.size() != outHeaders.size())
        {
            throw std::runtime_error("Column count mismatch at row " + std::to_string(rowIndex + 1) + " in " + filePath + ": expected " + std::to_string(outHeaders.size()) + ", got " + std::to_string(values.size()));
        }

        Row row;
        row.reserve(outHeaders.size());
        for (std::size_t i = 0; i < outHeaders.size(); ++i)
        {
            row.emplace(outHeaders[i], values[i]);
        }
        outTable.push_back(std::move(row));
    }
}

// CsvLoader — public API
Table CsvLoader::loadTable(const std::string &filePath)
{
    std::vector<std::string> headers;
    Table table;
    parseFile(filePath, headers, table);
    return table;
}

void CsvLoader::loadIntoDatabase(Database &db, const std::string &tableName, const std::string &filePath)
{
    std::vector<std::string> headers;
    Table table;
    parseFile(filePath, headers, table);
    db.schemas[tableName] = std::move(headers);
    db.tables[tableName] = std::move(table);
    db.markTableMutated(tableName);
}

void CsvLoader::saveTable(const std::string &filePath, const std::vector<std::string> &headers, const Table &table)
{
    if (headers.empty())
    {
        throw std::runtime_error("Cannot save table with empty headers: " + filePath);
    }

    std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open CSV file for writing: " + filePath);
    }

    std::string out;
    out.reserve((headers.size() + table.size() * headers.size()) * 12U);

    // Header row
    for (std::size_t i = 0; i < headers.size(); ++i)
    {
        if (i > 0)
            out.push_back(',');
        out += escapeCsvField(headers[i]);
    }
    out.push_back('\n');

    // Data rows — write in schema order so output is deterministic.
    for (const auto &row : table)
    {
        for (std::size_t i = 0; i < headers.size(); ++i)
        {
            if (i > 0)
                out.push_back(',');
            const auto it = row.find(headers[i]);
            out += escapeCsvField(it != row.end() ? it->second : std::string{});
        }
        out.push_back('\n');
    }

    file.write(out.data(), static_cast<std::streamsize>(out.size()));

    if (!file)
    {
        throw std::runtime_error("Write error on CSV file: " + filePath);
    }
}

// Expose private helpers via the CsvLoader:: scope so the header declaration
// is satisfied (they were declared private but defined here as file-static
// wrappers that forward to the anonymous-namespace implementations).
void CsvLoader::stripTrailingCR(std::string &s) noexcept
{
    ::stripTrailingCR(s);
}
void CsvLoader::stripUtf8Bom(std::string &s) noexcept
{
    ::stripUtf8Bom(s);
}

std::vector<std::string> CsvLoader::parseLine(const std::string &line)
{
    return parseCsvLine(line);
}

std::string CsvLoader::escapeField(const std::string &field)
{
    return escapeCsvField(field);
}