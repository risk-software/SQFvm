#include "ops_regex.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <boost/regex.hpp>

#include <optional>
#include <string>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    /// A pattern as the engine reads it: "expression/flags". Without a slash the flags are "gi".
    /// g finds or replaces every match, i ignores case. Other flag letters are accepted and ignored.
    struct parsed_pattern
    {
        std::string expression;
        bool global;
        bool ignore_case;
    };

    parsed_pattern parse_pattern(const std::string& pattern)
    {
        auto slash = pattern.rfind('/');
        if (slash != std::string::npos)
        {
            auto flags = pattern.substr(slash + 1);
            bool only_letters = flags.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
            if (only_letters)
            {
                return {
                    pattern.substr(0, slash),
                    flags.find('g') != std::string::npos,
                    flags.find('i') != std::string::npos,
                };
            }
        }
        return { pattern, true, true };
    }

    std::optional<boost::regex> compile_pattern(runtime& runtime, const parsed_pattern& pattern, const char* command)
    {
        try
        {
            boost::regex::flag_type flags = boost::regex::perl;
            if (pattern.ignore_case)
            {
                flags = flags | boost::regex::icase;
            }
            return boost::regex(pattern.expression, flags);
        }
        catch (const boost::regex_error& error)
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), command,
                std::string("invalid regular expression: ") + error.what()));
            return {};
        }
    }

    value regexmatch_string_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto pattern = parse_pattern(right.data<d_string, std::string>());
        auto expression = compile_pattern(runtime, pattern, "regexMatch");
        if (!expression)
        {
            return false;
        }
        auto haystack = left.data<d_string, std::string>();
        return boost::regex_match(haystack, *expression);
    }

    /// Each match is [[match, index], [group1, index], ...].
    value regexfind_string_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() < 1 || params->size() > 2 || !params->at(0).is<t_string>() || (params->size() == 2 && !params->at(1).is<t_scalar>()))
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "regexFind", "expected [pattern, startOffset]"));
            return value(std::make_shared<d_array>());
        }
        auto pattern = parse_pattern(params->at(0).data<d_string, std::string>());
        auto expression = compile_pattern(runtime, pattern, "regexFind");
        if (!expression)
        {
            return value(std::make_shared<d_array>());
        }
        auto haystack = left.data<d_string, std::string>();
        size_t offset = 0;
        if (params->size() == 2)
        {
            auto requested = params->at(1).data<d_scalar, float>();
            if (requested < 0 || requested > static_cast<float>(haystack.size()))
            {
                runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "regexFind", "startOffset is outside the string"));
                return value(std::make_shared<d_array>());
            }
            offset = static_cast<size_t>(requested);
        }
        std::vector<value> matches;
        auto begin = haystack.cbegin() + static_cast<std::ptrdiff_t>(offset);
        for (boost::sregex_iterator it(begin, haystack.cend(), *expression), end; it != end; ++it)
        {
            std::vector<value> groups;
            for (size_t group = 0; group < it->size(); group++)
            {
                auto& sub = (*it)[static_cast<int>(group)];
                auto index = sub.matched ? static_cast<float>(sub.first - haystack.cbegin()) : -1.0f;
                groups.push_back(value(std::make_shared<d_array>(std::vector<value>{ value(sub.str()), value(index) })));
            }
            matches.push_back(value(std::make_shared<d_array>(groups)));
            if (!pattern.global)
            {
                break;
            }
        }
        return value(std::make_shared<d_array>(matches));
    }

    /// The replacement uses Perl format: $&, $`, $', $1, $+{name}, \L and \U.
    value regexreplace_string_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto haystack = left.data<d_string, std::string>();
        auto params = right.data<d_array>();
        if (params->size() != 2 || !params->at(0).is<t_string>() || !params->at(1).is<t_string>())
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "regexReplace", "expected [pattern, replaceString]"));
            return haystack;
        }
        auto pattern = parse_pattern(params->at(0).data<d_string, std::string>());
        auto expression = compile_pattern(runtime, pattern, "regexReplace");
        if (!expression)
        {
            return haystack;
        }
        boost::regex_constants::match_flag_type flags = boost::format_perl;
        if (!pattern.global)
        {
            flags = flags | boost::format_first_only;
        }
        return boost::regex_replace(haystack, *expression, params->at(1).data<d_string, std::string>(), flags);
    }
}

void sqf::operators::ops_regex(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(binary(4, "regexMatch", t_string(), t_string(), "Returns true if the whole string matches the pattern (\"expression/flags\", default flags gi).", regexmatch_string_string));
    runtime.register_sqfop(binary(4, "regexFind", t_string(), t_array(), "regexFind [pattern, startOffset]: returns the matches, each as [[match, index], [group, index], ...].", regexfind_string_array));
    runtime.register_sqfop(binary(4, "regexReplace", t_string(), t_array(), "regexReplace [pattern, replaceString]: replaces matches using Perl format ($&, $1, $+{name}, \\L, \\U).", regexreplace_string_array));
}
