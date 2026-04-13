#include "storage/metadata_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#define METADATA_FILE "metadata.json"

namespace
{

    struct JVal
    {
        enum class Kind
        {
            Object,
            Array,
            String,
            Bool,
            Null
        } kind = Kind::Null;

        std::unordered_map<std::string, JVal> obj;
        std::vector<JVal> arr;
        std::string str;
        bool boolean = false;
    };

    //  Parser 
    class JsonParser
    {
    public:
        explicit JsonParser(std::string text) : src_(std::move(text)) {}

        JVal parse()
        {
            skip();
            JVal v = value();
            skip();
            if (pos_ < src_.size())
                fail("Unexpected trailing content");
            return v;
        }

    private:
        std::string src_;
        std::size_t pos_ = 0;

        bool eof() const noexcept { return pos_ >= src_.size(); }
        char cur() const noexcept { return eof() ? '\0' : src_[pos_]; }
        char adv()
        {
            if (eof())
                fail("Unexpected EOF");
            return src_[pos_++];
        }
        void skip()
        {
            while (!eof() && std::isspace(static_cast<unsigned char>(cur())))
                ++pos_;
        }

        void expect(char c)
        {
            skip();
            if (adv() != c)
                fail(std::string("Expected '") + c + "'");
        }

        [[noreturn]] void fail(const std::string &msg) const
        {
            throw std::runtime_error(
                "Metadata JSON error at pos " + std::to_string(pos_) + ": " + msg);
        }

        JVal value()
        {
            skip();
            const char c = cur();
            if (c == '{')
                return object();
            if (c == '[')
                return array();
            if (c == '"')
            {
                JVal v;
                v.kind = JVal::Kind::String;
                v.str = string();
                return v;
            }
            if (c == 't' || c == 'f')
            {
                JVal v;
                v.kind = JVal::Kind::Bool;
                v.boolean = boolean();
                return v;
            }
            if (c == 'n')
            {
                null();
                JVal v;
                v.kind = JVal::Kind::Null;
                return v;
            }
            fail(std::string("Unexpected character '") + c + "'");
        }

        JVal object()
        {
            JVal v;
            v.kind = JVal::Kind::Object;
            expect('{');
            skip();
            if (cur() == '}')
            {
                adv();
                return v;
            }
            while (true)
            {
                skip();
                if (cur() != '"')
                    fail("Expected string key");
                std::string key = string();
                skip();
                expect(':');
                v.obj.emplace(std::move(key), value());
                skip();
                const char d = adv();
                if (d == '}')
                    break;
                if (d != ',')
                    fail(std::string("Expected ',' or '}', got '") + d + "'");
            }
            return v;
        }

        JVal array()
        {
            JVal v;
            v.kind = JVal::Kind::Array;
            expect('[');
            skip();
            if (cur() == ']')
            {
                adv();
                return v;
            }
            while (true)
            {
                v.arr.push_back(value());
                skip();
                const char d = adv();
                if (d == ']')
                    break;
                if (d != ',')
                    fail(std::string("Expected ',' or ']', got '") + d + "'");
            }
            return v;
        }

        std::string string()
        {
            expect('"');
            std::string out;
            while (true)
            {
                const char c = adv();
                if (c == '"')
                    break;
                if (c == '\\')
                {
                    const char e = adv();
                    switch (e)
                    {
                    case '"':
                    case '\\':
                    case '/':
                        out += e;
                        break;
                    case 'b':
                        out += '\b';
                        break;
                    case 'f':
                        out += '\f';
                        break;
                    case 'n':
                        out += '\n';
                        break;
                    case 'r':
                        out += '\r';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    default:
                        fail(std::string("Bad escape \\") + e);
                    }
                }
                else
                {
                    out += c;
                }
            }
            return out;
        }

        bool boolean()
        {
            if (src_.compare(pos_, 4, "true") == 0)
            {
                pos_ += 4;
                return true;
            }
            if (src_.compare(pos_, 5, "false") == 0)
            {
                pos_ += 5;
                return false;
            }
            fail("Invalid boolean");
            return false;
        }

        void null()
        {
            if (src_.compare(pos_, 4, "null") != 0)
                fail("Invalid null");
            pos_ += 4;
        }
    };

    //  Extraction helpers 
    const std::unordered_map<std::string, JVal> &
    asObj(const JVal &v, const char *ctx)
    {
        if (v.kind != JVal::Kind::Object)
            throw std::runtime_error(std::string("Expected object: ") + ctx);
        return v.obj;
    }
    const std::vector<JVal> &
    asArr(const JVal &v, const char *ctx)
    {
        if (v.kind != JVal::Kind::Array)
            throw std::runtime_error(std::string("Expected array: ") + ctx);
        return v.arr;
    }
    std::string
    asStr(const JVal &v, const char *ctx)
    {
        if (v.kind != JVal::Kind::String)
            throw std::runtime_error(std::string("Expected string: ") + ctx);
        return v.str;
    }

    std::string reqStr(const std::unordered_map<std::string, JVal> &o,
                       const char *key, const char *ctx)
    {
        const auto it = o.find(key);
        if (it == o.end())
            throw std::runtime_error(
                std::string("Missing key '") + key + "' in " + ctx);
        return asStr(it->second, ctx);
    }

    std::optional<std::string> optStr(const std::unordered_map<std::string, JVal> &o,
                                      const char *key, const char *ctx)
    {
        const auto it = o.find(key);
        if (it == o.end() || it->second.kind == JVal::Kind::Null)
            return std::nullopt;
        return asStr(it->second, ctx);
    }

    bool optBool(const std::unordered_map<std::string, JVal> &o,
                 const char *key, const char * /*ctx*/, bool dflt)
    {
        const auto it = o.find(key);
        if (it == o.end())
            return dflt;
        if (it->second.kind == JVal::Kind::Bool)
            return it->second.boolean;
        return dflt;
    }

    //  Serialization helpers 
    std::string jsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (const char c : s)
        {
            switch (c)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }

    std::string readFile(const std::string &path)
    {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("Cannot open metadata: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

}

// MetadataCatalog
MetadataCatalog::MetadataCatalog(std::string dataDirectory)
    : dataDir_(std::move(dataDirectory)) {}

bool MetadataCatalog::metadataFileExists() const
{
    std::ifstream f(metadataFilePath());
    return f.is_open();
}

void MetadataCatalog::load()
{
    tables_.clear();
    if (!metadataFileExists())
        return;

    const JVal root = JsonParser(readFile(metadataFilePath())).parse();
    const auto &rootObj = asObj(root, "root");

    const auto tabIt = rootObj.find("tables");
    if (tabIt == rootObj.end())
        return;

    for (const auto &[tname, tval] : asObj(tabIt->second, "tables"))
    {
        const auto &tobj = asObj(tval, tname.c_str());
        TableMetadata meta;
        meta.file = reqStr(tobj, "file", tname.c_str());

        const auto colIt = tobj.find("columns");
        if (colIt == tobj.end())
            throw std::runtime_error("Missing 'columns' for table: " + tname);

        for (std::size_t i = 0; i < asArr(colIt->second, "columns").size(); ++i)
        {
            const auto &cobj = asObj(asArr(colIt->second, "columns")[i], "column");
            ColumnMetadata col;
            col.name = reqStr(cobj, "name", "column");
            col.type = reqStr(cobj, "type", "column");
            col.primaryKey = optBool(cobj, "primary_key", "column", false);
            col.unique = optBool(cobj, "unique", "column", false);
            col.notNull = optBool(cobj, "not_null", "column", false);
            col.foreignKey = optStr(cobj, "foreign_key", "column");
            meta.columns.push_back(std::move(col));
        }
        tables_[tname] = std::move(meta);
    }
}

void MetadataCatalog::save() const
{
    std::ofstream out(metadataFilePath());
    if (!out)
        throw std::runtime_error("Cannot write metadata: " + metadataFilePath());

    // Sort table names for deterministic output (diffs are readable).
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto &[k, _] : tables_)
        names.push_back(k);
    std::sort(names.begin(), names.end());

    out << "{\n  \"tables\": {";
    if (!names.empty())
        out << '\n';

    for (std::size_t ti = 0; ti < names.size(); ++ti)
    {
        const auto &tname = names[ti];
        const auto &tm = tables_.at(tname);

        out << "    \"" << jsonEscape(tname) << "\": {\n";
        out << "      \"file\": \"" << jsonEscape(tm.file) << "\",\n";
        out << "      \"columns\": [\n";

        for (std::size_t ci = 0; ci < tm.columns.size(); ++ci)
        {
            const auto &c = tm.columns[ci];
            out << "        { \"name\": \"" << jsonEscape(c.name)
                << "\", \"type\": \"" << jsonEscape(c.type) << '"';
            if (c.primaryKey)
                out << ", \"primary_key\": true";
            if (c.unique)
                out << ", \"unique\": true";
            if (c.notNull)
                out << ", \"not_null\": true";
            if (c.foreignKey.has_value())
                out << ", \"foreign_key\": \"" << jsonEscape(*c.foreignKey) << '"';
            out << " }";
            if (ci + 1 < tm.columns.size())
                out << ',';
            out << '\n';
        }

        out << "      ]\n    }";
        if (ti + 1 < names.size())
            out << ',';
        out << '\n';
    }

    // Handle empty tables object gracefully.
    if (names.empty())
        out << "}\n}\n";
    else
        out << "  }\n}\n";
}

// CRUD

bool MetadataCatalog::hasTable(const std::string &name) const noexcept
{
    return tables_.count(name) != 0;
}

const TableMetadata &MetadataCatalog::getTable(const std::string &name) const
{
    const auto it = tables_.find(name);
    if (it == tables_.end())
        throw std::runtime_error("Metadata not found for table: " + name);
    return it->second;
}

std::vector<std::string> MetadataCatalog::getColumnOrder(const std::string &name) const
{
    const auto &tm = getTable(name);
    std::vector<std::string> cols;
    cols.reserve(tm.columns.size());
    for (const auto &c : tm.columns)
        cols.push_back(c.name);
    return cols;
}

void MetadataCatalog::upsertTable(const std::string &name, TableMetadata meta)
{
    tables_[name] = std::move(meta);
}

void MetadataCatalog::removeTable(const std::string &name)
{
    tables_.erase(name);
}

const std::unordered_map<std::string, TableMetadata> &
MetadataCatalog::allTables() const noexcept
{
    return tables_;
}

const std::string &MetadataCatalog::dataDirectory() const noexcept { return dataDir_; }

std::string MetadataCatalog::metadataFilePath() const
{
    return dataDir_ + "/" + METADATA_FILE;
}

std::string MetadataCatalog::tableFilePath(const std::string &tableName) const
{
    return dataDir_ + "/" + getTable(tableName).file;
}