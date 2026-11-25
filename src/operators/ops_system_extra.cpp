#include "ops_system_extra.h"
#include "ops_generic.h"
#include "ops_hashmap.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_code.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace sqf::types
{
    struct t_for_c : public sqf::runtime::type::extend<t_for_c> { t_for_c() : extend() {} static const std::string name() { return "FOR"; } };

    /// The [init, condition, step] of a C-style for loop.
    class d_for_c : public sqf::runtime::data
    {
    public:
        using data_type = t_for_c;
        instruction_set init;
        instruction_set condition;
        instruction_set step;
    protected:
        bool do_equals(std::shared_ptr<sqf::runtime::data> other, bool invariant) const override { return this == other.get(); }
    public:
        std::string to_string_sqf() const override { return "for [...]"; }
        std::string to_string() const override { return "for [...]"; }
        sqf::runtime::type type() const override { return data_type(); }
        std::size_t hash() const override { return 0; }
    };
}

namespace
{
    // SQF-VM has no rendered frames; frame-based diagnostics report a steady 60 frames per second.
    constexpr float nominal_fps = 60.0f;

    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
    }

    std::string lowercase(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    /// breakTo name: leaves every scope inside the named one and continues in the named scope.
    value breakto_string(runtime& runtime, value::cref right)
    {
        auto target = right.data<d_string, std::string>();
        auto& context = runtime.context_active();
        size_t frames_above = 0;
        for (auto it = context.frames_rbegin(); it != context.frames_rend(); ++it, ++frames_above)
        {
            if (it->scope_name() == target)
            {
                for (; frames_above > 0; --frames_above)
                {
                    context.pop_frame();
                }
                return {};
            }
        }
        runtime.__logmsg(err::ScopeNameNotFound(position(runtime), target));
        return {};
    }

    /// for [{init}, {condition}, {step}] (the deprecated C-style form, still supported by the engine).
    value for_array(runtime& runtime, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() != 3 || !params->at(0).is<t_code>() || !params->at(1).is<t_code>() || !params->at(2).is<t_code>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "for", "expected [{init}, {condition}, {step}]"));
            return {};
        }
        auto loop = std::make_shared<d_for_c>();
        loop->init = params->at(0).data<d_code, instruction_set>();
        loop->condition = params->at(1).data<d_code, instruction_set>();
        loop->step = params->at(2).data<d_code, instruction_set>();
        return value(loop);
    }

    value do_for_c_code(runtime& runtime, value::cref left, value::cref right)
    {
        class behavior_for_c_exit : public frame::behavior
        {
        private:
            enum class phase { init, condition, body, step };
            std::shared_ptr<d_for_c> m_loop;
            instruction_set m_body;
            phase m_phase;
            phase m_next_phase;
        public:
            behavior_for_c_exit(std::shared_ptr<d_for_c> loop, instruction_set body) :
                m_loop(loop), m_body(body), m_phase(phase::init), m_next_phase(phase::init) {}
            sqf::runtime::instruction_set get_instruction_set(sqf::runtime::frame& frame) override
            {
                m_phase = m_next_phase;
                switch (m_phase)
                {
                case phase::condition: return m_loop->condition;
                case phase::body: return m_body;
                case phase::step: return m_loop->step;
                default: return m_loop->init;
                }
            }
            result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
            {
                switch (m_phase)
                {
                case phase::init:
                case phase::step:
                    m_next_phase = phase::condition;
                    break;
                case phase::condition:
                {
                    auto res = runtime.context_active().pop_value();
                    if (!res.has_value() || !res->is<t_boolean>())
                    {
                        runtime.__logmsg(err::TypeMissmatch(frame.diag_info_from_position(), t_boolean(), res.has_value() ? res->type() : t_nothing()));
                        return result::ok;
                    }
                    if (!res->data<d_boolean, bool>())
                    {
                        return result::ok;
                    }
                    m_next_phase = phase::body;
                    break;
                }
                case phase::body:
                    m_next_phase = phase::step;
                    break;
                }
                runtime.context_active().clear_values();
                return result::exchange;
            }
        };
        auto loop = left.data<d_for_c>();
        frame f(runtime.default_value_scope(), loop->init, std::make_shared<behavior_for_c_exit>(loop, right.data<d_code, instruction_set>()));
        f.is_loop(true);
        runtime.context_active().push_frame(f);
        return {};
    }

    value fileexists_string(runtime& runtime, value::cref right)
    {
        auto path = right.data<d_string, std::string>();
        return runtime.fileio().get_info(path, {}).has_value();
    }

    /// Translations from stringtable.xml in the virtual file system: key (lower case) to the
    /// <Original> text, or <English> when there is no <Original>.
    class stringtable
    {
    private:
        std::unordered_map<std::string, std::string> m_entries;
        bool m_loaded = false;

        static std::string decode_entities(std::string text)
        {
            const std::pair<const char*, const char*> entities[] = { {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"} };
            for (auto& [entity, replacement] : entities)
            {
                size_t found;
                while ((found = text.find(entity)) != std::string::npos)
                {
                    text.replace(found, std::char_traits<char>::length(entity), replacement);
                }
            }
            return text;
        }
        static std::optional<std::string> element_text(const std::string& block, const std::string& element)
        {
            auto open = block.find("<" + element + ">");
            if (open == std::string::npos)
            {
                return {};
            }
            auto start = open + element.size() + 2;
            auto close = block.find("</" + element + ">", start);
            if (close == std::string::npos)
            {
                return {};
            }
            return decode_entities(block.substr(start, close - start));
        }
        void parse(const std::string& xml)
        {
            size_t position = 0;
            while ((position = xml.find("<Key", position)) != std::string::npos)
            {
                auto id_start = xml.find("ID=\"", position);
                auto end = xml.find("</Key>", position);
                if (id_start == std::string::npos || end == std::string::npos || id_start > end)
                {
                    break;
                }
                id_start += 4;
                auto id_end = xml.find('"', id_start);
                auto block = xml.substr(id_start, end - id_start);
                auto text = element_text(block, "Original");
                if (!text)
                {
                    text = element_text(block, "English");
                }
                if (text)
                {
                    m_entries[lowercase(xml.substr(id_start, id_end - id_start))] = *text;
                }
                position = end + 6;
            }
        }
    public:
        std::optional<std::string> lookup(runtime& runtime, std::string key)
        {
            if (!m_loaded)
            {
                m_loaded = true;
                auto info = runtime.fileio().get_info("stringtable.xml", {});
                if (info.has_value())
                {
                    parse(runtime.fileio().read_file(*info));
                }
            }
            if (!key.empty() && key[0] == '$')
            {
                key = key.substr(1);
            }
            auto found = m_entries.find(lowercase(key));
            if (found == m_entries.end())
            {
                return {};
            }
            return found->second;
        }
    };
    stringtable& loaded_stringtable()
    {
        static stringtable table;
        return table;
    }

    value localize_string(runtime& runtime, value::cref right)
    {
        auto key = right.data<d_string, std::string>();
        auto text = loaded_stringtable().lookup(runtime, key);
        if (!text)
        {
            runtime.__logmsg(err::WarningMessage(position(runtime), "localize", "String " + key + " not found"));
            return std::string();
        }
        return *text;
    }
    value islocalized_string(runtime& runtime, value::cref right)
    {
        return loaded_stringtable().lookup(runtime, right.data<d_string, std::string>()).has_value();
    }

    /// diag_codePerformance [code, arguments, cycles, ignoreTimeLimit]: runs the code unscheduled and
    /// returns [average milliseconds, cycles run]. Stops after one second unless ignoreTimeLimit is set.
    value diag_codeperformance_array(runtime& runtime, value::cref right)
    {
        class behavior_code_performance_exit : public frame::behavior
        {
        private:
            size_t m_wanted;
            size_t m_done;
            bool m_ignore_time_limit;
            bool m_could_suspend;
            value m_arguments;
            std::chrono::steady_clock::time_point m_start;
        public:
            behavior_code_performance_exit(size_t wanted, bool ignore_time_limit, bool could_suspend, value arguments) :
                m_wanted(wanted), m_done(0), m_ignore_time_limit(ignore_time_limit), m_could_suspend(could_suspend),
                m_arguments(arguments), m_start(std::chrono::steady_clock::now()) {}
            result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
            {
                m_done++;
                auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
                runtime.context_active().clear_values();
                if (m_done < m_wanted && (m_ignore_time_limit || elapsed < 1000.0))
                {
                    frame.clear_value_scope();
                    frame["_this"] = m_arguments;
                    return result::seek_start;
                }
                runtime.context_active().can_suspend(m_could_suspend);
                runtime.context_active().push_value(value(std::make_shared<d_array>(std::vector<value>{
                    static_cast<float>(elapsed / static_cast<double>(m_done)), static_cast<float>(m_done) })));
                return result::ok;
            }
        };
        auto params = right.data<d_array>();
        if (params->size() < 1 || params->size() > 4 || !params->at(0).is<t_code>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "diag_codePerformance", "expected [code, arguments, cycles, ignoreTimeLimit]"));
            return {};
        }
        auto arguments = params->size() >= 2 ? params->at(1) : value{};
        auto cycles = params->size() >= 3 ? std::max(1.0f, params->at(2).data_try<d_scalar, float>(100000.0f)) : 100000.0f;
        bool ignore_time_limit = params->size() == 4 && params->at(3).data_try<d_boolean, bool>(false);
        auto& context = runtime.context_active();
        frame f(runtime.default_value_scope(), params->at(0).data<d_code, instruction_set>(),
            std::make_shared<behavior_code_performance_exit>(static_cast<size_t>(cycles), ignore_time_limit, context.can_suspend(), arguments));
        f["_this"] = arguments;
        context.can_suspend(false);
        context.push_frame(f);
        return {};
    }

    /// diag_activeSQFScripts: [[scriptName, fileName, isRunning, currentLine], ...] for scheduled scripts.
    value diag_activesqfscripts_(runtime& runtime)
    {
        std::vector<value> scripts;
        for (auto it = runtime.context_begin(); it != runtime.context_end(); ++it)
        {
            auto& context = *it;
            if (!context->can_suspend() || context->empty())
            {
                continue;
            }
            auto info = context->current_frame().diag_info_from_position();
            scripts.push_back(value(std::make_shared<d_array>(std::vector<value>{
                context->name(), info.path.virtual_, !context->suspended(), static_cast<float>(info.line) })));
        }
        return std::make_shared<d_array>(scripts);
    }

    /// diag_stacktrace (2.06): one entry per scope, innermost last, as [scopeName, fileName, line, variables].
    value diag_stacktrace_(runtime& runtime)
    {
        std::vector<value> entries;
        auto& context = runtime.context_active();
        for (auto it = context.frames_rbegin(); it != context.frames_rend(); ++it)
        {
            auto info = it->diag_info_from_position();
            auto variables = std::make_shared<d_hashmap>();
            for (auto& variable : *it)
            {
                variables->map()[value(variable.first)] = variable.second;
            }
            entries.insert(entries.begin(), value(std::make_shared<d_array>(std::vector<value>{
                std::string(it->scope_name()), info.path.virtual_, static_cast<float>(info.line), value(variables) })));
        }
        return std::make_shared<d_array>(entries);
    }

    float seconds_since_start(runtime& runtime)
    {
        return std::chrono::duration<float>(std::chrono::system_clock::now() - runtime.runtime_timestamp()).count();
    }
}

void sqf::operators::ops_system_extra(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(unary("breakTo", t_string(), "Leaves every scope inside the named scope and continues in it.", breakto_string));
    runtime.register_sqfop(unary("for", t_array(), "for [{init}, {condition}, {step}]: the C-style for loop.", for_array));
    runtime.register_sqfop(binary(4, "do", t_for_c(), t_code(), "Runs the C-style for loop.", do_for_c_code));
    runtime.register_sqfop(unary("fileExists", t_string(), "True if the file exists in the virtual file system.", fileexists_string));
    runtime.register_sqfop(unary("localize", t_string(), "Text of the stringtable.xml key, or an empty string.", localize_string));
    runtime.register_sqfop(unary("isLocalized", t_string(), "True if stringtable.xml has the key.", islocalized_string));
    runtime.register_sqfop(unary("publicVariable", t_string(), "SQF-VM is a single machine, so there is nobody to send the variable to.",
        [](::sqf::runtime::runtime& runtime, value::cref right) -> value { return {}; }));
    runtime.register_sqfop(unary("publicVariableServer", t_string(), "SQF-VM is the server, so the variable is already there.",
        [](::sqf::runtime::runtime& runtime, value::cref right) -> value { return {}; }));
    runtime.register_sqfop(binary(4, "publicVariableClient", t_scalar(), t_string(), "SQF-VM has no clients.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return {}; }));
    runtime.register_sqfop(nular("isUIContext", "False: SQF-VM has no user interface.",
        [](::sqf::runtime::runtime& runtime) -> value { return false; }));
    runtime.register_sqfop(unary("diag_codePerformance", t_array(), "diag_codePerformance [code, arguments, cycles, ignoreTimeLimit]", diag_codeperformance_array));
    runtime.register_sqfop(nular("diag_activeSQFScripts", "[[scriptName, fileName, isRunning, currentLine], ...]", diag_activesqfscripts_));
    runtime.register_sqfop(nular("diag_activeScripts", "[spawned, execVM, exec, execFSM] script counts.",
        [](::sqf::runtime::runtime& runtime) -> value
        {
            float scheduled = 0;
            for (auto it = runtime.context_begin(); it != runtime.context_end(); ++it)
            {
                if ((*it)->can_suspend() && !(*it)->empty()) { scheduled++; }
            }
            return std::make_shared<d_array>(std::vector<value>{ scheduled, 0.0f, 0.0f, 0.0f });
        }));
    runtime.register_sqfop(nular("diag_stacktrace", "[[scopeName, fileName, line, variables], ...], innermost scope last.", diag_stacktrace_));
    runtime.register_sqfop(nular("diag_fps", "Frames per second; SQF-VM reports a nominal 60.",
        [](::sqf::runtime::runtime& runtime) -> value { return nominal_fps; }));
    runtime.register_sqfop(nular("diag_fpsMin", "Lowest frames per second; SQF-VM reports a nominal 60.",
        [](::sqf::runtime::runtime& runtime) -> value { return nominal_fps; }));
    runtime.register_sqfop(nular("diag_deltaTime", "Seconds per frame at the nominal 60 frames per second.",
        [](::sqf::runtime::runtime& runtime) -> value { return 1.0f / nominal_fps; }));
    runtime.register_sqfop(nular("diag_frameNo", "Frame number at the nominal 60 frames per second since the VM started.",
        [](::sqf::runtime::runtime& runtime) -> value { return std::floor(seconds_since_start(runtime) * nominal_fps); }));
}
