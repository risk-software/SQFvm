#pragma once
#include "../runtime/instruction.h"
#include "../runtime/value.h"
#include "../runtime/data.h"
#include "../runtime/runtime.h"

#include <string>
#include <string_view>

namespace sqf::opcodes
{
    class assign_to : public sqf::runtime::instruction
    {
    private:
#if _DEBUG
        std::string ___TYPE = "assign_to";
#endif
        std::string m_variable_name;

    public:
        assign_to(std::string value) : m_variable_name(value) {}
        assign_to(std::string_view value) : m_variable_name(value.begin(), value.end()) {}
        virtual void execute(sqf::runtime::runtime& vm) const override
        {
            auto& context = vm.context_active();

            auto value = vm.context_active().pop_value();
            if (!value.has_value())
            {
                if (context.weak_error_handling())
                {
                    vm.__logmsg(logmessage::runtime::FoundNoValueWeak(diag_info()));
                }
                else
                {
                    vm.__logmsg(logmessage::runtime::FoundNoValue(diag_info()));
                }
                return;
            }
            else if (value->is<sqf::types::t_nothing>())
            {
                vm.__logmsg(logmessage::runtime::AssigningNilValue(diag_info(), m_variable_name));
            }
            if (m_variable_name.empty()) { return; }
            // Variables holding a final value (compileFinal) cannot be overwritten.
            auto is_final_value = [](const sqf::runtime::value& existing) { return !existing.empty() && existing.data<sqf::runtime::data>()->is_final(); };
            if (m_variable_name[0] == '_')
            {
                for (auto it = context.frames_rbegin(); it != context.frames_rend(); ++it)
                {
                    if (it->contains(m_variable_name))
                    {
                        if (is_final_value((*it)[m_variable_name]))
                        {
                            vm.__logmsg(logmessage::runtime::WarningMessage(diag_info(), "assignment", "variable " + m_variable_name + " holds a final value and cannot be overwritten"));
                            return;
                        }
                        (*it)[m_variable_name] = *value;
                        return;
                    }
                    // Same boundary as variable lookup, so privateAll scopes cannot change parent locals.
                    if (!it->bubble_variable())
                    {
                        break;
                    }
                }
                context.current_frame()[m_variable_name] = *value;
            }
            else
            {
                auto globals = context.current_frame().globals_value_scope();
                if (globals->contains(m_variable_name) && is_final_value(globals->at(m_variable_name)))
                {
                    vm.__logmsg(logmessage::runtime::WarningMessage(diag_info(), "assignment", "variable " + m_variable_name + " holds a final value and cannot be overwritten"));
                    return;
                }
                globals->at(m_variable_name) = *value;
            }
        }
        virtual std::string to_string() const override { return std::string("ASSIGNTO ") + m_variable_name; }
        std::string_view variable_name() const { return m_variable_name; }

        virtual std::optional<std::string> reconstruct(
            std::vector<sqf::runtime::instruction::sptr>::const_reverse_iterator& current,
            std::vector<sqf::runtime::instruction::sptr>::const_reverse_iterator end,
            short parent_precedence, bool left_from_binary) const override
        {
            if (++current == end)
            {
                return {};
            }
            auto exp = (*current)->reconstruct(current, end, 10, false);
            if (!exp.has_value())
            {
                return {};
            }
            return m_variable_name + " = " + *exp;
        }

        virtual bool equals(const instruction* p_other) const override
        {
            auto casted = dynamic_cast<const assign_to*>(p_other);
            return casted != nullptr && casted->m_variable_name == m_variable_name;
        }
    };
}