#pragma once
#include "../runtime/data.h"
#include "../runtime/type.h"
#include "../runtime/value.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include <sstream>


namespace sqf
{
    namespace runtime
    {
        struct t_hashmap : public type::extend<t_hashmap> { t_hashmap() : extend() {} static const std::string name() { return "HASHMAP"; } };
        class runtime;
    }
    namespace types
    {
        class d_hashmap : public sqf::runtime::data
        {
        private:
            std::unordered_map<sqf::runtime::value, sqf::runtime::value> m_map;
            // compileFinal: no changes at all. sealed (#flags): no new keys. no_copy (#flags): + fails.
            bool m_final = false;
            bool m_sealed = false;
            bool m_no_copy = false;
            // #clone methods of a HashMap object, from base to derived class.
            std::vector<sqf::runtime::value> m_clone_methods;
        public:
            using data_type = sqf::runtime::t_hashmap;
            bool is_final() const override { return m_final; }
            void make_final() { m_final = true; }
            bool is_sealed() const { return m_sealed; }
            void sealed(bool flag) { m_sealed = flag; }
            bool is_no_copy() const { return m_no_copy; }
            void no_copy(bool flag) { m_no_copy = flag; }
            const std::vector<sqf::runtime::value>& clone_methods() const { return m_clone_methods; }
            void clone_methods(std::vector<sqf::runtime::value> methods) { m_clone_methods = std::move(methods); }
        protected:
            virtual bool do_equals(std::shared_ptr<data> other, bool invariant) const
            {
                auto& other_map = std::static_pointer_cast<d_hashmap>(other)->map();
                auto& self_map = m_map;

                return other_map == self_map;
            }
        public:
            d_hashmap() = default;
            d_hashmap(std::unordered_map<sqf::runtime::value, sqf::runtime::value> map) : m_map(map) {}

            sqf::runtime::type type() const override { return data_type(); }
            virtual std::size_t hash() const override
            {
                size_t hash = 0x9e3779b9;
                for (auto& it : m_map)
                {
                    hash ^= std::hash<sqf::runtime::value>()(it.first) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                    hash ^= std::hash<sqf::runtime::value>()(it.second) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
                }
                return hash;
            }

            std::string to_string_sqf() const override
            {
                std::stringstream sstream;
                sstream << "[";
                if (m_map.size() > 0)
                {
                    for (auto it : m_map)
                    {
                        sstream << "[" << it.first.to_string_sqf() << "," << it.second.to_string_sqf() << "]" << ",";
                    }
                    sstream.seekp(-1, std::ios_base::end);
                }
                sstream << "]";
                return sstream.str();
            }
            std::string to_string() const override
            {
                std::stringstream sstream;
                sstream << "[";
                if (m_map.size() > 0)
                {
                    for (auto it : m_map)
                    {
                        sstream << it.first.to_string() << ": " << it.second.to_string() << ",";
                    }
                    sstream.seekp(-1, std::ios_base::end);
                }
                sstream << "}";
                return sstream.str();
            }

            std::unordered_map<sqf::runtime::value, sqf::runtime::value>& map() { return m_map; }
        };
    }

    namespace operators
    {
        void ops_hashmap(::sqf::runtime::runtime& runtime);
    }
}