#include "ops_string.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"

#include "../runtime/d_string.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_array.h"
#include "../runtime/d_code.h"

#include <algorithm>
#include <sstream>
#include <cmath>
#include <cctype>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;
using namespace std::string_literals;

namespace
{
    value count_string(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_string, std::string>();
        return r.length();
    }
    value toupper_string(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_string, std::string>();
        std::transform(r.begin(), r.end(), r.begin(), [](char& c) { return (char)std::toupper((int)c); });
        return r;
    }
    value tolower_string(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_string, std::string>();
        std::transform(r.begin(), r.end(), r.begin(), [](char& c) { return (char)std::tolower((int)c); });
        return r;
    }
    value select_string_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto str = left.data<d_string, std::string>();
        auto arr = right.data<d_array>();
        if (arr->empty())
        {
            runtime.__logmsg(err::ExpectedArrayToHaveElements(runtime.context_active().current_frame().diag_info_from_position()));
            return {};
        }
        if (!arr->at(0).is<t_scalar>())
        {
            runtime.__logmsg(err::ExpectedArrayTypeMissmatch(runtime.context_active().current_frame().diag_info_from_position(), 0, t_scalar(), arr->at(0).type()));
            return {};
        }
        int start = static_cast<int>(std::round(arr->at(0).data<d_scalar, float>()));
        if (start < 0)
        {
            runtime.__logmsg(err::NegativeIndexWeak(runtime.context_active().current_frame().diag_info_from_position()));
            runtime.__logmsg(err::ReturningEmptyString(runtime.context_active().current_frame().diag_info_from_position()));
            return ""s;
        }
        if (start >= static_cast<int>(str.length()))
        {
            runtime.__logmsg(err::IndexOutOfRangeWeak(runtime.context_active().current_frame().diag_info_from_position(), str.length(), start));
            runtime.__logmsg(err::ReturningEmptyString(runtime.context_active().current_frame().diag_info_from_position()));
            return ""s;
        }
        if (arr->size() >= 2)
        {
            if (!arr->at(1).is<t_scalar>())
            {
                runtime.__logmsg(err::ExpectedArrayTypeMissmatch(runtime.context_active().current_frame().diag_info_from_position(), 1, t_scalar(), arr->at(1).type()));
                return {};
            }
            int length = static_cast<int>(std::round(arr->at(1).data<d_scalar, float>()));
            if (length < 0)
            {
                runtime.__logmsg(err::NegativeIndexWeak(runtime.context_active().current_frame().diag_info_from_position()));
                runtime.__logmsg(err::ReturningEmptyString(runtime.context_active().current_frame().diag_info_from_position()));
                return ""s;
            }
            return str.substr(start, length);
        }
        return str.substr(start);
    }
    value format_array(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_array>();
        if (r->empty())
        {
            runtime.__logmsg(err::ExpectedArrayToHaveElementsWeak(runtime.context_active().current_frame().diag_info_from_position()));
            runtime.__logmsg(err::ReturningEmptyString(runtime.context_active().current_frame().diag_info_from_position()));
            return ""s;
        }
        if (!r->at(0).is<t_string>())
        {
            runtime.__logmsg(err::ExpectedArrayTypeMissmatchWeak(runtime.context_active().current_frame().diag_info_from_position(), 0, t_string(), r->at(0).type()));
            runtime.__logmsg(err::ReturningEmptyString(runtime.context_active().current_frame().diag_info_from_position()));
            return ""s;
        }
        auto format = r->at(0).data<d_string, std::string>();
        std::stringstream sstream;
        size_t off = 0;
        size_t newoff;
        while ((newoff = format.find(L'%', off)) != std::string::npos)
        {
            sstream << format.substr(off, newoff - off);
            newoff++;

            if (!(format[newoff] >= '0' && format[newoff] <= '9'))
            {
                runtime.__logmsg(err::FormatInvalidPlaceholder(runtime.context_active().current_frame().diag_info_from_position(), format[newoff], newoff));
                newoff++;
            }
            else
            {
                size_t end;
                for (end = newoff; format[end] >= '0' && format[end] <= '9'; ++end);
                auto num = std::stoi(format.substr(newoff, end - newoff));
                newoff = end;
                if (num >= static_cast<int>(r->size()))
                {
                    runtime.__logmsg(err::IndexOutOfRangeWeak(runtime.context_active().current_frame().diag_info_from_position(), r->size(), num));
                }
                else if (r->at(num).is<t_string>())
                {
                    sstream << r->at(num).data<d_string, std::string>();
                }
                else
                {
                    sstream << r->at(num).to_string_sqf();
                }
            }
            off = newoff;
        }
        sstream << (format.size() >= off ? format.substr(off) : "");
        return sstream.str();
    }
    value toarray_string(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_string, std::string>();
        auto arr = std::vector<value>(r.size());
        for (size_t i = 0; i < r.size(); i++)
        {
            arr[i] = static_cast<int>(r[i]);
        }
        return value(arr);
    }
    value tostring_array(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_array>();
        std::stringstream sstream;
        for (size_t i = 0; i < r->size(); i++)
        {
            auto& val = r->at(i);
            if (val.is<t_scalar>())
            {
                sstream << static_cast<char>(val.data<d_scalar, int>());
            }
            else
            {
                runtime.__logmsg(err::ExpectedArrayTypeMissmatch(runtime.context_active().current_frame().diag_info_from_position(), i, t_scalar(), val.type()));
            }
        }
        return sstream.str();
    }
    value joinstring_array_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto l = left.data<d_array>();
        auto r = right.data<d_string, std::string>();
        std::stringstream sstream;
        bool separator = false;
        for (auto& it : *l)
        {
            if (separator)
            {
                if (it.is<t_string>())
                {
                    sstream << r << it.data<d_string, std::string>();
                }
                else
                {
                    sstream << r << it.data()->to_string_sqf();
                }
            }
            else
            {
                separator = true;
                if (it.is<t_string>())
                {
                    sstream << it.data<d_string, std::string>();
                }
                else
                {
                    sstream << it.data()->to_string_sqf();
                }
            }
        }
        return sstream.str();
    }
    value plus_string_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto l = left.data<d_string, std::string>();
        auto r = right.data<d_string, std::string>();
        std::stringstream sstream;
        sstream << l << r;
        return sstream.str();
    }
    value find_string_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto l = left.data<d_string, std::string>();
        auto r = right.data<d_string, std::string>();
        size_t res = l.find(r);
        return res == std::string::npos ? -1.f : res;
    }
    value splitstring_string_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto l = left.data<d_string, std::string>();
        auto r = right.data<d_string, std::string>();
        std::vector<value> values;
        if (r.empty())
        {
            for (auto c : l)
            {
                values.push_back(c);
            }
            return values;
        }
        // Avoid using actual strtok method due to concerns regarding the way
        // it works (Reentrancy variance)
        size_t match_length = 0;
        for (size_t i = 0; i < l.length(); i++)
        {
            char lc = l[i];
            bool hit = false;
            for (auto rc : r)
            {
                if (lc == rc)
                {
                    if (match_length > 0)
                    {
                        values.push_back(l.substr(i - match_length, match_length));
                        match_length = 0;
                    }
                    hit = true;
                    break;
                }
            }
            if (!hit)
            {
                match_length++;
            }
        }
        if (match_length != 0)
        {
            values.push_back(l.substr(l.size() - match_length));
        }
        return values;
    }
    value parsenumber_string(runtime& runtime, value::cref right)
    {
        return value(strtof(right.data<d_string, std::string>().c_str(), nullptr));
    }
    value endl_(runtime& runtime)
    {
        return "\r\n"s;
    }
    /// toLowerANSI and toUpperANSI (1.96) convert ASCII letters only, which is what makes them fast.
    value toloweransi_string(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_string, std::string>();
        std::transform(r.begin(), r.end(), r.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
        return r;
    }
    value toupperansi_string(runtime& runtime, value::cref right)
    {
        auto r = right.data<d_string, std::string>();
        std::transform(r.begin(), r.end(), r.begin(), [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; });
        return r;
    }

    enum class trim_side { both, start, end };
    std::string trim_chars(std::string text, const std::string& chars, trim_side side)
    {
        if (side != trim_side::end)
        {
            auto first = text.find_first_not_of(chars);
            text.erase(0, first == std::string::npos ? text.size() : first);
        }
        if (side != trim_side::start)
        {
            auto last = text.find_last_not_of(chars);
            text.erase(last == std::string::npos ? 0 : last + 1);
        }
        return text;
    }
    /// trim (2.02) removes spaces, tabs and line breaks from both ends.
    value trim_string(runtime& runtime, value::cref right)
    {
        return trim_chars(right.data<d_string, std::string>(), " \t\r\n", trim_side::both);
    }
    /// string trim [chars, where]: where 0 trims both ends, 1 the start and 2 the end.
    value trim_string_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() != 2 || !params->at(0).is<t_string>() || !params->at(1).is<t_scalar>())
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "trim", "expected [chars, where]"));
            return left;
        }
        auto where = static_cast<int>(params->at(1).data<d_scalar, float>());
        auto side = where == 1 ? trim_side::start : where == 2 ? trim_side::end : trim_side::both;
        return trim_chars(left.data<d_string, std::string>(), params->at(0).data<d_string, std::string>(), side);
    }

    /// forceUnicode (2.02) changes how the next string command counts characters. SQF-VM works on
    /// bytes throughout, so it is accepted and has no effect.
    value forceunicode_scalar(runtime& runtime, value::cref right)
    {
        return {};
    }

    /// toString code (2.02) returns the code's text without the outer braces.
    value tostring_code(runtime& runtime, value::cref right)
    {
        auto text = right.data<sqf::runtime::data>()->to_string_sqf();
        if (text.size() >= 2 && text.front() == '{' && text.back() == '}')
        {
            text = text.substr(1, text.size() - 2);
        }
        return trim_chars(text, " \t\r\n", trim_side::both);
    }
}
void sqf::operators::ops_string(sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;

    runtime.register_sqfop(nular("endl", "Creates a string containing a line break (`\r\n`).", endl_));
    runtime.register_sqfop(unary("count", t_string(), "Can be used to count: the number of characters in a string.", count_string));
    runtime.register_sqfop(unary("toLower", t_string(), "Converts the supplied string to all lowercase characters.", tolower_string));
    runtime.register_sqfop(unary("toUpper", t_string(), "Converts the supplied string to all uppercase characters.", toupper_string));
    runtime.register_sqfop(binary(4, "select", t_string(), t_array(), "Selects a range of characters in provided string, starting at element 0 index, ending at either end of the string or the provided element 1 length.", select_string_array));
    runtime.register_sqfop(unary("format", t_array(), "Composes a string containing other variables or other variable types. Converts any variable type to a string.", format_array));
    runtime.register_sqfop(unary("toArray", t_string(), "Converts the supplied String into an Array of Numbers.", toarray_string));
    runtime.register_sqfop(unary("toString", t_array(), "Converts the supplied String into an Array of Numbers.", tostring_array));
    runtime.register_sqfop(binary(4, "joinString", t_array(), t_string(), "Joins array into String with provided separator. Array can be of mixed types, all elements will be converted to String prior to joining, but the fastest operation is on the array of Strings.", joinstring_array_string));
    runtime.register_sqfop(binary(6, "+", t_string(), t_string(), "Concatinates two strings together.", plus_string_string));
    runtime.register_sqfop(binary(4, "find", t_string(), t_string(), "Searches for a string within a string. Returns the 0 based index on success or -1 if not found.", find_string_string));
    runtime.register_sqfop(binary(4, "splitString", t_string(), t_string(), "An SQF version of C++ strtok. "
        "Splits given string str into an array of tokens according to given delimiters. "
        "In addition, if empty string "" is used for delimiters, str is split by each character.", splitstring_string_string));
    runtime.register_sqfop(unary("parseNumber", t_string(), "Converts the supplied String into a Scalar.", parsenumber_string));
    runtime.register_sqfop(unary("toLowerANSI", t_string(), "Converts ASCII letters of the string to lower case.", toloweransi_string));
    runtime.register_sqfop(unary("toUpperANSI", t_string(), "Converts ASCII letters of the string to upper case.", toupperansi_string));
    runtime.register_sqfop(unary("trim", t_string(), "Removes spaces, tabs and line breaks from both ends of the string.", trim_string));
    runtime.register_sqfop(binary(4, "trim", t_string(), t_array(), "trim [chars, where]: removes the characters from both ends (0), the start (1) or the end (2).", trim_string_array));
    runtime.register_sqfop(unary("forceUnicode", t_scalar(), "Accepted for compatibility; SQF-VM string commands always work on bytes.", forceunicode_scalar));
    runtime.register_sqfop(unary("toString", t_code(), "Returns the code as a string, without the outer braces.", tostring_code));
}