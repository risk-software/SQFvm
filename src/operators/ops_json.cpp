#include "ops_json.h"
#include "ops_hashmap.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    void write_json_string(std::ostringstream& out, const std::string& text)
    {
        out << '"';
        for (unsigned char c : text)
        {
            switch (c)
            {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            default:
                if (c < 0x20)
                {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    out << escaped;
                }
                else
                {
                    out << static_cast<char>(c);
                }
            }
        }
        out << '"';
    }

    /// Writes one value. Returns false for types JSON cannot hold; containers write null for those.
    bool write_json(std::ostringstream& out, value::cref val)
    {
        if (val.empty() || val.is<t_nothing>())
        {
            out << "null";
            return true;
        }
        if (val.is<t_boolean>())
        {
            out << (val.data<d_boolean, bool>() ? "true" : "false");
            return true;
        }
        if (val.is<t_scalar>())
        {
            auto number = val.data<d_scalar, float>();
            if (!std::isfinite(number))
            {
                out << "null";
                return true;
            }
            out << val.to_string_sqf();
            return true;
        }
        if (val.is<t_string>())
        {
            write_json_string(out, val.data<d_string, std::string>());
            return true;
        }
        if (val.is<t_array>())
        {
            auto arr = val.data<d_array>();
            out << '[';
            for (size_t i = 0; i < arr->size(); i++)
            {
                if (i > 0)
                {
                    out << ',';
                }
                if (!write_json(out, arr->at(i)))
                {
                    out << "null";
                }
            }
            out << ']';
            return true;
        }
        if (val.is<t_hashmap>())
        {
            out << '{';
            bool first = true;
            for (auto& pair : val.data<d_hashmap>()->map())
            {
                if (!first)
                {
                    out << ',';
                }
                first = false;
                // JSON keys are strings; other key types are written in their SQF form.
                write_json_string(out, pair.first.is<t_string>() ? pair.first.data<d_string, std::string>() : pair.first.to_string_sqf());
                out << ':';
                if (!write_json(out, pair.second))
                {
                    out << "null";
                }
            }
            out << '}';
            return true;
        }
        return false;
    }

    value tojson_any(runtime& runtime, value::cref right)
    {
        std::ostringstream out;
        if (!write_json(out, right))
        {
            return std::string();
        }
        return out.str();
    }

    class json_reader
    {
    private:
        const std::string& m_text;
        size_t m_position;
        std::string m_error;

        void skip_whitespace()
        {
            while (m_position < m_text.size() && (m_text[m_position] == ' ' || m_text[m_position] == '\t' || m_text[m_position] == '\n' || m_text[m_position] == '\r'))
            {
                m_position++;
            }
        }
        bool fail(const std::string& message)
        {
            if (m_error.empty())
            {
                m_error = message + " at position " + std::to_string(m_position);
            }
            return false;
        }
        bool consume_word(const char* word)
        {
            auto length = std::char_traits<char>::length(word);
            if (m_text.compare(m_position, length, word) == 0)
            {
                m_position += length;
                return true;
            }
            return false;
        }
        static void append_utf8(std::string& out, unsigned long code_point)
        {
            if (code_point < 0x80)
            {
                out += static_cast<char>(code_point);
            }
            else if (code_point < 0x800)
            {
                out += static_cast<char>(0xC0 | (code_point >> 6));
                out += static_cast<char>(0x80 | (code_point & 0x3F));
            }
            else if (code_point < 0x10000)
            {
                out += static_cast<char>(0xE0 | (code_point >> 12));
                out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code_point & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (code_point >> 18));
                out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code_point & 0x3F));
            }
        }
        std::optional<unsigned long> read_hex4()
        {
            if (m_position + 4 > m_text.size())
            {
                return {};
            }
            unsigned long result = 0;
            for (size_t i = 0; i < 4; i++)
            {
                char c = m_text[m_position++];
                result <<= 4;
                if (c >= '0' && c <= '9') { result |= c - '0'; }
                else if (c >= 'a' && c <= 'f') { result |= c - 'a' + 10; }
                else if (c >= 'A' && c <= 'F') { result |= c - 'A' + 10; }
                else { return {}; }
            }
            return result;
        }
        bool read_string(std::string& out)
        {
            m_position++; // opening quote
            while (m_position < m_text.size())
            {
                char c = m_text[m_position++];
                if (c == '"')
                {
                    return true;
                }
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (m_position >= m_text.size())
                {
                    break;
                }
                char escape = m_text[m_position++];
                switch (escape)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    auto code_point = read_hex4();
                    if (!code_point)
                    {
                        return fail("invalid \\u escape");
                    }
                    // A high surrogate is followed by a low one for characters outside the basic plane.
                    if (*code_point >= 0xD800 && *code_point <= 0xDBFF && consume_word("\\u"))
                    {
                        auto low = read_hex4();
                        if (!low || *low < 0xDC00 || *low > 0xDFFF)
                        {
                            return fail("invalid surrogate pair");
                        }
                        *code_point = 0x10000 + ((*code_point - 0xD800) << 10) + (*low - 0xDC00);
                    }
                    append_utf8(out, *code_point);
                    break;
                }
                default:
                    return fail("invalid escape");
                }
            }
            return fail("unterminated string");
        }
        bool read_number(value& out)
        {
            auto start = m_position;
            if (m_text[m_position] == '-')
            {
                m_position++;
            }
            while (m_position < m_text.size() && (std::isdigit(static_cast<unsigned char>(m_text[m_position])) || m_text[m_position] == '.' || m_text[m_position] == 'e' || m_text[m_position] == 'E' || m_text[m_position] == '+' || m_text[m_position] == '-'))
            {
                m_position++;
            }
            auto number_text = m_text.substr(start, m_position - start);
            char* end = nullptr;
            auto number = std::strtod(number_text.c_str(), &end);
            if (number_text.empty() || end != number_text.c_str() + number_text.size())
            {
                return fail("invalid number");
            }
            out = static_cast<float>(number);
            return true;
        }
    public:
        explicit json_reader(const std::string& text) : m_text(text), m_position(0) {}
        const std::string& error() const { return m_error; }

        bool read_value(value& out, size_t depth = 0)
        {
            if (depth > 512)
            {
                return fail("nesting too deep");
            }
            skip_whitespace();
            if (m_position >= m_text.size())
            {
                return fail("unexpected end");
            }
            char c = m_text[m_position];
            if (c == '"')
            {
                std::string text;
                if (!read_string(text))
                {
                    return false;
                }
                out = text;
                return true;
            }
            if (c == '[')
            {
                m_position++;
                std::vector<value> elements;
                skip_whitespace();
                if (m_position < m_text.size() && m_text[m_position] == ']')
                {
                    m_position++;
                    out = value(std::make_shared<d_array>(elements));
                    return true;
                }
                while (true)
                {
                    value element;
                    if (!read_value(element, depth + 1))
                    {
                        return false;
                    }
                    elements.push_back(element);
                    skip_whitespace();
                    if (m_position < m_text.size() && m_text[m_position] == ',')
                    {
                        m_position++;
                        continue;
                    }
                    if (m_position < m_text.size() && m_text[m_position] == ']')
                    {
                        m_position++;
                        out = value(std::make_shared<d_array>(elements));
                        return true;
                    }
                    return fail("expected , or ]");
                }
            }
            if (c == '{')
            {
                m_position++;
                auto map = std::make_shared<d_hashmap>();
                skip_whitespace();
                if (m_position < m_text.size() && m_text[m_position] == '}')
                {
                    m_position++;
                    out = value(map);
                    return true;
                }
                while (true)
                {
                    skip_whitespace();
                    if (m_position >= m_text.size() || m_text[m_position] != '"')
                    {
                        return fail("expected a string key");
                    }
                    std::string key;
                    if (!read_string(key))
                    {
                        return false;
                    }
                    skip_whitespace();
                    if (m_position >= m_text.size() || m_text[m_position] != ':')
                    {
                        return fail("expected :");
                    }
                    m_position++;
                    value element;
                    if (!read_value(element, depth + 1))
                    {
                        return false;
                    }
                    map->map()[value(key)] = element;
                    skip_whitespace();
                    if (m_position < m_text.size() && m_text[m_position] == ',')
                    {
                        m_position++;
                        continue;
                    }
                    if (m_position < m_text.size() && m_text[m_position] == '}')
                    {
                        m_position++;
                        out = value(map);
                        return true;
                    }
                    return fail("expected , or }");
                }
            }
            if (consume_word("true"))
            {
                out = true;
                return true;
            }
            if (consume_word("false"))
            {
                out = false;
                return true;
            }
            if (consume_word("null"))
            {
                out = value{};
                return true;
            }
            if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            {
                return read_number(out);
            }
            return fail("unexpected character");
        }
        bool at_end()
        {
            skip_whitespace();
            return m_position == m_text.size();
        }
    };

    value fromjson_string(runtime& runtime, value::cref right)
    {
        auto text = right.data<d_string, std::string>();
        json_reader reader(text);
        value result;
        if (!reader.read_value(result) || !reader.at_end())
        {
            auto message = reader.error().empty() ? std::string("trailing characters") : reader.error();
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "fromJSON", message));
            return {};
        }
        return result;
    }
}

namespace
{
    /// Reader for parseSimpleArray: numbers, strings in double or single quotes (quotes doubled to
    /// escape them), true, false, nil, null, <null> and nested arrays, with any amount of whitespace.
    class simple_array_reader
    {
    private:
        const std::string& m_text;
        size_t m_position;

        void skip_whitespace()
        {
            while (m_position < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_position])))
            {
                m_position++;
            }
        }
        bool consume_keyword(const char* word)
        {
            auto length = std::char_traits<char>::length(word);
            if (m_text.size() - m_position < length)
            {
                return false;
            }
            for (size_t i = 0; i < length; i++)
            {
                if (std::tolower(static_cast<unsigned char>(m_text[m_position + i])) != word[i])
                {
                    return false;
                }
            }
            m_position += length;
            return true;
        }
    public:
        explicit simple_array_reader(const std::string& text) : m_text(text), m_position(0) {}

        bool read_value(value& out, size_t depth)
        {
            if (depth > 512)
            {
                return false;
            }
            skip_whitespace();
            if (m_position >= m_text.size())
            {
                return false;
            }
            char c = m_text[m_position];
            if (c == '[')
            {
                return read_array(out, depth);
            }
            if (c == '"' || c == '\'')
            {
                char quote = c;
                std::string text;
                m_position++;
                while (m_position < m_text.size())
                {
                    if (m_text[m_position] == quote)
                    {
                        if (m_position + 1 < m_text.size() && m_text[m_position + 1] == quote)
                        {
                            text += quote;
                            m_position += 2;
                            continue;
                        }
                        m_position++;
                        out = text;
                        return true;
                    }
                    text += m_text[m_position++];
                }
                return false;
            }
            if (consume_keyword("true")) { out = true; return true; }
            if (consume_keyword("false")) { out = false; return true; }
            if (consume_keyword("nil") || consume_keyword("null") || consume_keyword("<null>")) { out = value{}; return true; }
            char* end = nullptr;
            auto number = std::strtod(m_text.c_str() + m_position, &end);
            if (end == m_text.c_str() + m_position)
            {
                return false;
            }
            m_position = static_cast<size_t>(end - m_text.c_str());
            out = static_cast<float>(number);
            return true;
        }
        bool read_array(value& out, size_t depth)
        {
            skip_whitespace();
            if (m_position >= m_text.size() || m_text[m_position] != '[')
            {
                return false;
            }
            m_position++;
            std::vector<value> elements;
            skip_whitespace();
            if (m_position < m_text.size() && m_text[m_position] == ']')
            {
                m_position++;
                out = value(std::make_shared<d_array>(elements));
                return true;
            }
            while (true)
            {
                value element;
                if (!read_value(element, depth + 1))
                {
                    return false;
                }
                elements.push_back(element);
                skip_whitespace();
                if (m_position < m_text.size() && m_text[m_position] == ',')
                {
                    m_position++;
                    continue;
                }
                if (m_position < m_text.size() && m_text[m_position] == ']')
                {
                    m_position++;
                    out = value(std::make_shared<d_array>(elements));
                    return true;
                }
                return false;
            }
        }
        bool at_end()
        {
            skip_whitespace();
            return m_position == m_text.size();
        }
    };

    value parsesimplearray_string(runtime& runtime, value::cref right)
    {
        auto text = right.data<d_string, std::string>();
        simple_array_reader reader(text);
        value result;
        if (!reader.read_array(result, 0) || !reader.at_end())
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "parseSimpleArray", "the string is not a simple array"));
            return value(std::make_shared<d_array>());
        }
        return result;
    }
}

void sqf::operators::ops_json(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(unary("toJSON", t_any(), "Converts numbers, strings, booleans, nil, arrays and HashMaps to a JSON string. Other types become null inside containers and an empty string on their own.", tojson_any));
    runtime.register_sqfop(unary("fromJSON", t_string(), "Converts a JSON string to SQF values. Objects become HashMaps and null becomes nil.", fromjson_string));
    runtime.register_sqfop(unary("parseSimpleArray", t_string(), "Converts a string holding a simple array (numbers, strings, booleans, nil and arrays) into an array, without compiling it.", parsesimplearray_string));
}
