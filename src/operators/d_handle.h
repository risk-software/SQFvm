#pragma once
#include "../runtime/data.h"
#include "../runtime/type.h"
#include "../runtime/value.h"

#include <memory>
#include <string>

namespace sqf
{
    namespace types
    {
        struct t_teammember : public sqf::runtime::type::extend<t_teammember> { t_teammember() : extend() {} static const std::string name() { return "TEAM_MEMBER"; } };
        struct t_diaryrecord : public sqf::runtime::type::extend<t_diaryrecord> { t_diaryrecord() : extend() {} static const std::string name() { return "DIARY_RECORD"; } };

        /// A reference to an engine entity that SQF-VM does not model in detail, such as a location,
        /// display, control, task, team member or diary record. Id 0 is the null handle of its type.
        class d_handle : public sqf::runtime::data
        {
        private:
            sqf::runtime::type m_type;
            size_t m_id;
            std::string m_label;
        protected:
            bool do_equals(std::shared_ptr<sqf::runtime::data> other, bool invariant) const override
            {
                auto other_handle = std::static_pointer_cast<d_handle>(other);
                return other_handle->m_type == m_type && other_handle->m_id == m_id;
            }
        public:
            d_handle(sqf::runtime::type type, size_t id = 0, std::string label = {}) : m_type(type), m_id(id), m_label(std::move(label)) {}

            static size_t next_id()
            {
                static size_t counter = 0;
                return ++counter;
            }

            bool is_null() const { return m_id == 0; }
            size_t id() const { return m_id; }
            const std::string& label() const { return m_label; }

            sqf::runtime::type type() const override { return m_type; }
            std::size_t hash() const override { return std::hash<size_t>()(m_id) ^ std::hash<std::string_view>()(m_type.to_string()); }
            std::string to_string_sqf() const override { return to_string(); }
            std::string to_string() const override
            {
                if (is_null())
                {
                    return "No " + std::string(m_type.to_string());
                }
                return m_label.empty() ? std::string(m_type.to_string()) + " " + std::to_string(m_id) : m_label;
            }
        };
    }
}
