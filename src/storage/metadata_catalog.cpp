#include "storage/metadata_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

#define METADATA_FILE "metadata.json"

namespace
{

    std::string jsonEscape(const std::string &s);

    struct JVal
    {
        enum class Kind
        {
            Object,
            Array,
            String,
            Number,
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
            if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            {
                JVal v;
                v.kind = JVal::Kind::Number;
                v.str = number();
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

        std::string number()
        {
            const std::size_t start = pos_;
            if (cur() == '-')
                ++pos_;
            if (!std::isdigit(static_cast<unsigned char>(cur())))
                fail("Invalid number");

            if (cur() == '0')
            {
                ++pos_;
            }
            else
            {
                while (!eof() && std::isdigit(static_cast<unsigned char>(cur())))
                    ++pos_;
            }

            if (!eof() && cur() == '.')
            {
                ++pos_;
                if (eof() || !std::isdigit(static_cast<unsigned char>(cur())))
                    fail("Invalid number fraction");
                while (!eof() && std::isdigit(static_cast<unsigned char>(cur())))
                    ++pos_;
            }

            if (!eof() && (cur() == 'e' || cur() == 'E'))
            {
                ++pos_;
                if (!eof() && (cur() == '+' || cur() == '-'))
                    ++pos_;
                if (eof() || !std::isdigit(static_cast<unsigned char>(cur())))
                    fail("Invalid number exponent");
                while (!eof() && std::isdigit(static_cast<unsigned char>(cur())))
                    ++pos_;
            }

            return src_.substr(start, pos_ - start);
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

    std::string reqStr(const std::unordered_map<std::string, JVal> &o, const char *key, const char *ctx)
    {
        const auto it = o.find(key);
        if (it == o.end())
            throw std::runtime_error(
                std::string("Missing key '") + key + "' in " + ctx);
        return asStr(it->second, ctx);
    }

    const JVal &reqVal(const std::unordered_map<std::string, JVal> &o, const char *key, const char *ctx)
    {
        const auto it = o.find(key);
        if (it == o.end())
            throw std::runtime_error(
                std::string("Missing key '") + key + "' in " + ctx);
        return it->second;
    }

    std::optional<std::string> optStr(const std::unordered_map<std::string, JVal> &o, const char *key, const char *ctx)
    {
        const auto it = o.find(key);
        if (it == o.end() || it->second.kind == JVal::Kind::Null)
            return std::nullopt;
        return asStr(it->second, ctx);
    }

    bool optBool(const std::unordered_map<std::string, JVal> &o, const char *key, const char * /*ctx*/, bool dflt)
    {
        const auto it = o.find(key);
        if (it == o.end())
            return dflt;
        if (it->second.kind == JVal::Kind::Bool)
            return it->second.boolean;
        return dflt;
    }

    std::string trim(std::string s)
    {
        const auto isWs = [](unsigned char c)
        { return std::isspace(c) != 0; };
        const auto b = std::find_if_not(s.begin(), s.end(), isWs);
        const auto e = std::find_if_not(s.rbegin(), s.rend(), isWs).base();
        if (b >= e)
            return {};
        return std::string(b, e);
    }

    std::string regexEscape(const std::string &s)
    {
        static const std::regex kMeta(R"([.^$|()\[\]{}*+?\\])");
        return std::regex_replace(s, kMeta, R"(\\$&)");
    }

    bool isNumericToken(const std::string &s)
    {
        static const std::regex kNum(R"(^[+-]?(\d+(\.\d+)?|\.\d+)([eE][+-]?\d+)?$)");
        return std::regex_match(s, kNum);
    }

    std::string sqlQuote(const std::string &s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        out.push_back('\'');
        for (char c : s)
        {
            out.push_back(c);
            if (c == '\'')
                out.push_back('\'');
        }
        out.push_back('\'');
        return out;
    }

    std::optional<std::string> unquoteSqlString(const std::string &tok)
    {
        if (tok.size() < 2 || tok.front() != '\'' || tok.back() != '\'')
            return std::nullopt;
        std::string out;
        out.reserve(tok.size() - 2);
        for (std::size_t i = 1; i + 1 < tok.size(); ++i)
        {
            if (tok[i] == '\'' && i + 1 < tok.size() - 1 && tok[i + 1] == '\'')
            {
                out.push_back('\'');
                ++i;
            }
            else
            {
                out.push_back(tok[i]);
            }
        }
        return out;
    }

    std::string scalarToSqlLiteral(const JVal &v, const char *ctx)
    {
        if (v.kind == JVal::Kind::Number)
            return v.str;
        if (v.kind == JVal::Kind::String)
            return sqlQuote(v.str);
        throw std::runtime_error(std::string("Expected scalar for ") + ctx);
    }

    std::vector<std::string> splitCommaOutsideQuotes(const std::string &s)
    {
        std::vector<std::string> out;
        std::string cur;
        bool inQuote = false;

        for (std::size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            if (c == '\'')
            {
                cur.push_back(c);
                if (inQuote && i + 1 < s.size() && s[i + 1] == '\'')
                {
                    cur.push_back('\'');
                    ++i;
                }
                else
                {
                    inQuote = !inQuote;
                }
                continue;
            }
            if (c == ',' && !inQuote)
            {
                out.push_back(trim(cur));
                cur.clear();
                continue;
            }
            cur.push_back(c);
        }
        if (inQuote)
            return {};
        out.push_back(trim(cur));
        return out;
    }

    std::optional<std::vector<std::string>> parseEnumListTokens(const std::string &expr)
    {
        const auto parts = splitCommaOutsideQuotes(expr);
        if (parts.empty())
            return std::nullopt;

        std::vector<std::string> values;
        values.reserve(parts.size());
        for (const auto &p : parts)
        {
            if (p.empty())
                return std::nullopt;
            const auto u = unquoteSqlString(p);
            if (!u)
                return std::nullopt;
            values.push_back(*u);
        }
        return values;
    }

    std::string scalarToJson(const std::string &tok)
    {
        const std::string t = trim(tok);
        if (const auto uq = unquoteSqlString(t))
            return "\"" + jsonEscape(*uq) + "\"";
        if (isNumericToken(t))
            return t;
        return "\"" + jsonEscape(t) + "\"";
    }

    std::string parseCheckObject(const std::unordered_map<std::string, JVal> &o, const std::string &columnName)
    {
        const std::string type = trim(reqStr(o, "type", "check"));
        std::string typeLower = type;
        std::transform(typeLower.begin(), typeLower.end(), typeLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (typeLower == "enum")
        {
            const auto &vals = asArr(reqVal(o, "values", "check"), "check.values");
            if (vals.empty())
                throw std::runtime_error("check enum values cannot be empty");
            std::ostringstream expr;
            expr << columnName << " IN( ";
            for (std::size_t i = 0; i < vals.size(); ++i)
            {
                if (vals[i].kind != JVal::Kind::String)
                    throw std::runtime_error("check enum values must be strings");
                expr << sqlQuote(vals[i].str);
                if (i + 1 < vals.size())
                    expr << " , ";
            }
            expr << ")";
            return expr.str();
        }

        if (typeLower == "range")
        {
            const std::string lo = scalarToSqlLiteral(reqVal(o, "min", "check"), "check.min");
            const std::string hi = scalarToSqlLiteral(reqVal(o, "max", "check"), "check.max");
            return columnName + " BETWEEN " + lo + " AND " + hi;
        }

        if (typeLower == "comparison")
        {
            const std::string op = trim(reqStr(o, "operator", "check"));
            const std::string rhs = scalarToSqlLiteral(reqVal(o, "value", "check"), "check.value");
            return columnName + " " + op + " " + rhs;
        }

        if (typeLower == "expression")
            return reqStr(o, "sql", "check");

        throw std::runtime_error("Unsupported check type: " + type);
    }

    std::string checkExprToJson(const std::string &expr, const std::string &columnName)
    {
        const std::string e = trim(expr);

        const std::regex inPattern(
            "^\\s*" + regexEscape(columnName) +
                "\\s+IN\\s*\\((.*)\\)\\s*$",
            std::regex::icase);
        std::smatch m;

        std::string enumBody;
        bool tryEnum = false;
        if (std::regex_match(e, m, inPattern) && m.size() >= 2)
        {
            enumBody = trim(m[1].str());
            tryEnum = true;
        }
        else
        {
            enumBody = e;
            if (enumBody.find(',') != std::string::npos)
                tryEnum = true;
        }

        if (tryEnum)
        {
            if (const auto vals = parseEnumListTokens(enumBody))
            {
                std::ostringstream out;
                out << "{ \"type\": \"enum\", \"values\": [";
                for (std::size_t i = 0; i < vals->size(); ++i)
                {
                    out << "\"" << jsonEscape((*vals)[i]) << "\"";
                    if (i + 1 < vals->size())
                        out << ", ";
                }
                out << "] }";
                return out.str();
            }
        }

        const std::regex rangePattern(
            "^\\s*" + regexEscape(columnName) +
                "\\s+BETWEEN\\s+(.+?)\\s+AND\\s+(.+?)\\s*$",
            std::regex::icase);
        if (std::regex_match(e, m, rangePattern) && m.size() >= 3)
        {
            return "{ \"type\": \"range\", \"min\": " + scalarToJson(m[1].str()) +
                   ", \"max\": " + scalarToJson(m[2].str()) + " }";
        }

        const std::regex cmpPattern(
            "^\\s*" + regexEscape(columnName) +
                "\\s*(<=|>=|<>|!=|==|=|<|>)\\s*(.+?)\\s*$",
            std::regex::icase);
        if (std::regex_match(e, m, cmpPattern) && m.size() >= 3)
        {
            return "{ \"type\": \"comparison\", \"operator\": \"" +
                   jsonEscape(trim(m[1].str())) + "\", \"value\": " +
                   scalarToJson(m[2].str()) + " }";
        }

        return "{ \"type\": \"expression\", \"sql\": \"" +
               jsonEscape(e) + "\" }";
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
            if (const auto chkIt = cobj.find("check"); chkIt != cobj.end())
            {
                if (chkIt->second.kind == JVal::Kind::String)
                {
                    col.checkExpr = chkIt->second.str;
                }
                else if (chkIt->second.kind == JVal::Kind::Object)
                {
                    col.checkExpr = parseCheckObject(asObj(chkIt->second, "check"), col.name);
                }
                else
                {
                    throw std::runtime_error("Invalid 'check' type in metadata for column: " + col.name);
                }
            }
            meta.columns.push_back(std::move(col));
        }

        if (const auto tcIt = tobj.find("table_checks"); tcIt != tobj.end())
        {
            for (const auto &j : asArr(tcIt->second, "table_checks"))
                meta.tableChecks.push_back(asStr(j, "table_check"));
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
            if (c.checkExpr.has_value())
                out << ", \"check\": " << checkExprToJson(*c.checkExpr, c.name);
            out << " }";
            if (ci + 1 < tm.columns.size())
                out << ',';
            out << '\n';
        }

        out << "      ]";
        if (!tm.tableChecks.empty())
        {
            out << ",\n      \"table_checks\": [\n";
            for (std::size_t i = 0; i < tm.tableChecks.size(); ++i)
            {
                out << "        \"" << jsonEscape(tm.tableChecks[i]) << "\"";
                if (i + 1 < tm.tableChecks.size())
                    out << ',';
                out << '\n';
            }
            out << "      ]\n";
        }
        else
        {
            out << '\n';
        }

        out << "    }";
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