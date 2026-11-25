#include "ops_hashmap.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/util.h"

#include "../runtime/d_string.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_array.h"
#include "../runtime/instruction_set.h"
#include "../runtime/d_code.h"

#include <array>



namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    /// Final HashMaps cannot change; sealed HashMap objects cannot get new keys.
    bool can_write(runtime& runtime, const std::shared_ptr<d_hashmap>& map, const value& key)
    {
        if (map->is_final())
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "HashMap", "the HashMap is final and cannot be changed"));
            return false;
        }
        if (map->is_sealed() && map->map().find(key) == map->map().end())
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "HashMap", "the HashMap object is sealed; new keys cannot be added"));
            return false;
        }
        return true;
    }
    value createhashmap_(runtime& runtime)
    {
        return std::make_shared<d_hashmap>();
    }
    value createhashmapfromarray_array(runtime& runtime, value::cref right)
    {
        std::unordered_map<sqf::runtime::value, sqf::runtime::value> hashmap;
        auto arr = right.data<d_array>();
        for (size_t i = 0; i < arr->size(); i++)
        {
            auto& it = arr->at(i);
            if (it.is<t_array>())
            {
                auto subArr = it.data<d_array>();
                if (subArr->size() == 2)
                {
                    auto& key = subArr->at(0);
                    auto& value = subArr->at(1);
                    // ToDo: Check key-type matches
                    hashmap[key] = value;
                }
                else
                {
                    runtime.__logmsg(err::ExpectedArraySizeMissmatch(
                        runtime.context_active().current_frame().diag_info_from_position(),
                        2,
                        2,
                        subArr->size()));
                }
            }
            else
            {
                runtime.__logmsg(err::ExpectedArrayTypeMissmatch(
                    runtime.context_active().current_frame().diag_info_from_position(),
                    i,
                    t_array(),
                    it.type()));
            }
        }
        return std::make_shared<d_hashmap>(hashmap);
    }
    value set_hashmap_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto data = left.data<d_hashmap>();
        auto arr = right.data<d_array>();
        if (arr->size() != 2 && arr->size() != 3)
        {
            runtime.__logmsg(err::ExpectedArraySizeMissmatch(
                runtime.context_active().current_frame().diag_info_from_position(),
                2,
                3,
                arr->size()));
            return {};
        }
        auto& key = arr->at(0);
        if (!can_write(runtime, data, key))
        {
            return {};
        }
        // insertOnly (2.08): only set the value when the key is new.
        bool insert_only = arr->size() == 3 && arr->at(2).data_try<d_boolean, bool>(false);
        bool exists = data->map().find(key) != data->map().end();
        if (!(exists && insert_only))
        {
            data->map()[key] = arr->at(1);
        }
        // true when a value was overwritten, false when the key is new.
        return exists && !insert_only;
    }
    value get_hashmap_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto data = left.data<d_hashmap>();
        auto res = data->map().find(right);
        if (res != data->map().end())
        {
            return res->second;
        }
        else
        {
            // ToDo: Log warning about key not found
        }
        return {};
    }
    value deleteat_hashmap_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto data = left.data<d_hashmap>();
        if (!can_write(runtime, data, right))
        {
            return {};
        }
        auto res = data->map().find(right);
        if (res != data->map().end())
        {
            auto val = *res;
            data->map().erase(right);
            return val.second;
        }
        else
        {
            // ToDo: Log warning about key not found
        }
        return {};
    }
    value in_any_hashmap(runtime& runtime, value::cref left, value::cref right)
    {
        auto data = right.data<d_hashmap>();
        return data->map().find(left) != data->map().end();
    }
    value count_hashmap(runtime& runtime, value::cref right)
    {
        return right.data<d_hashmap>()->map().size();
    }
    value keys_hashmap(runtime& runtime, value::cref right)
    {
        std::vector<value> keys;
        auto data = right.data<d_hashmap>();
        for (auto& it : data->map())
        {
            keys.push_back(it.first);
        }
        return std::make_shared<d_array>(keys);
    }

    /// getOrDefault [key, default, setDefault] (setDefault since 2.04).
    value getordefault_hashmap_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto data = left.data<d_hashmap>();
        auto arr = right.data<d_array>();
        if (arr->size() < 1 || arr->size() > 3)
        {
            runtime.__logmsg(err::ExpectedArraySizeMissmatch(runtime.context_active().current_frame().diag_info_from_position(), 1, 3, arr->size()));
            return {};
        }
        auto found = data->map().find(arr->at(0));
        if (found != data->map().end())
        {
            return found->second;
        }
        auto fallback = arr->size() >= 2 ? arr->at(1) : value{};
        if (arr->size() == 3 && arr->at(2).data_try<d_boolean, bool>(false) && can_write(runtime, data, arr->at(0)))
        {
            data->map()[arr->at(0)] = fallback;
        }
        return fallback;
    }

    /// getOrDefaultCall [key, code, set] (2.12): runs the code only when the key is missing.
    value getordefaultcall_hashmap_array(runtime& runtime, value::cref left, value::cref right)
    {
        class behavior_getordefaultcall_exit : public frame::behavior
        {
        private:
            std::shared_ptr<d_hashmap> m_map;
            value m_key;
            bool m_store;
        public:
            behavior_getordefaultcall_exit(std::shared_ptr<d_hashmap> map, value key, bool store) : m_map(map), m_key(key), m_store(store) {}
            virtual result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
            {
                auto res = runtime.context_active().pop_value();
                auto computed = res.has_value() ? *res : value{};
                if (m_store && can_write(runtime, m_map, m_key))
                {
                    m_map->map()[m_key] = computed;
                }
                runtime.context_active().clear_values();
                runtime.context_active().push_value(computed);
                return result::ok;
            }
        };
        auto data = left.data<d_hashmap>();
        auto arr = right.data<d_array>();
        if (arr->size() < 2 || arr->size() > 3 || !arr->at(1).is<t_code>())
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "getOrDefaultCall", "expected [key, code, set]"));
            return {};
        }
        auto found = data->map().find(arr->at(0));
        if (found != data->map().end())
        {
            return found->second;
        }
        bool store = arr->size() == 3 && arr->at(2).data_try<d_boolean, bool>(false);
        frame f(runtime.default_value_scope(), arr->at(1).data<d_code, instruction_set>(), std::make_shared<behavior_getordefaultcall_exit>(data, arr->at(0), store));
        runtime.context_active().push_frame(f);
        return {};
    }

    void merge_into(runtime& runtime, std::shared_ptr<d_hashmap> target, std::shared_ptr<d_hashmap> source, bool overwrite)
    {
        for (auto& pair : source->map())
        {
            if (!can_write(runtime, target, pair.first))
            {
                return;
            }
            if (overwrite)
            {
                target->map()[pair.first] = pair.second;
            }
            else
            {
                target->map().insert(pair);
            }
        }
    }
    value merge_hashmap_hashmap(runtime& runtime, value::cref left, value::cref right)
    {
        merge_into(runtime, left.data<d_hashmap>(), right.data<d_hashmap>(), false);
        return {};
    }
    value merge_hashmap_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto arr = right.data<d_array>();
        if (arr->size() < 1 || arr->size() > 2 || !arr->at(0).is<t_hashmap>())
        {
            runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "merge", "expected [hashMap, overwriteExisting]"));
            return {};
        }
        bool overwrite = arr->size() == 2 && arr->at(1).data_try<d_boolean, bool>(false);
        merge_into(runtime, left.data<d_hashmap>(), arr->at(0).data<d_hashmap>(), overwrite);
        return {};
    }

    value values_hashmap(runtime& runtime, value::cref right)
    {
        std::vector<value> values;
        for (auto& it : right.data<d_hashmap>()->map())
        {
            values.push_back(it.second);
        }
        return std::make_shared<d_array>(values);
    }

    /// [[keys...], [values...]] when split, otherwise [[key, value], ...].
    value hashmap_to_array(std::shared_ptr<d_hashmap> map, bool split)
    {
        std::vector<value> keys;
        std::vector<value> values;
        std::vector<value> pairs;
        for (auto& it : map->map())
        {
            if (split)
            {
                keys.push_back(it.first);
                values.push_back(it.second);
            }
            else
            {
                pairs.push_back(value(std::make_shared<d_array>(std::vector<value>{ it.first, it.second })));
            }
        }
        if (split)
        {
            return std::make_shared<d_array>(std::vector<value>{ value(std::make_shared<d_array>(keys)), value(std::make_shared<d_array>(values)) });
        }
        return std::make_shared<d_array>(pairs);
    }
    value toarray_hashmap(runtime& runtime, value::cref right)
    {
        return hashmap_to_array(right.data<d_hashmap>(), true);
    }
    value toarray_hashmap_boolean(runtime& runtime, value::cref left, value::cref right)
    {
        return hashmap_to_array(left.data<d_hashmap>(), right.data<d_boolean, bool>());
    }

    void insert_pairs(runtime& runtime, std::shared_ptr<d_hashmap> map, std::shared_ptr<d_array> pairs)
    {
        for (size_t i = 0; i < pairs->size(); i++)
        {
            auto pair = pairs->at(i).data_try<d_array>();
            if (!pair || pair->size() != 2)
            {
                runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "HashMap", "expected [key, value] pairs"));
                return;
            }
            if (!can_write(runtime, map, pair->at(0)))
            {
                return;
            }
            map->map()[pair->at(0)] = pair->at(1);
        }
    }
    void insert_split(runtime& runtime, std::shared_ptr<d_hashmap> map, std::shared_ptr<d_array> keys, std::shared_ptr<d_array> values)
    {
        for (size_t i = 0; i < keys->size(); i++)
        {
            if (!can_write(runtime, map, keys->at(i)))
            {
                return;
            }
            map->map()[keys->at(i)] = i < values->size() ? values->at(i) : value{};
        }
    }
    /// insert [[key, value], ...] or insert [splitArray, keysAndValues] (2.04).
    value insert_hashmap_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto map = left.data<d_hashmap>();
        auto arr = right.data<d_array>();
        if (arr->size() == 2 && arr->at(0).is<t_boolean>())
        {
            auto content = arr->at(1).data_try<d_array>();
            if (!content)
            {
                runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "insert", "expected [splitArray, keysAndValues]"));
                return {};
            }
            if (!arr->at(0).data<d_boolean, bool>())
            {
                insert_pairs(runtime, map, content);
                return {};
            }
            if (content->size() != 2 || !content->at(0).is<t_array>() || !content->at(1).is<t_array>())
            {
                runtime.__logmsg(err::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "insert", "expected [[keys], [values]]"));
                return {};
            }
            insert_split(runtime, map, content->at(0).data<d_array>(), content->at(1).data<d_array>());
            return {};
        }
        insert_pairs(runtime, map, arr);
        return {};
    }

    /// keys createHashMapFromArray values (2.04). Missing values are nil.
    value createhashmapfromarray_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto map = std::make_shared<d_hashmap>();
        insert_split(runtime, map, left.data<d_array>(), right.data<d_array>());
        return map;
    }
}

void sqf::operators::ops_hashmap(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(nular("createHashMap", "Creates a hashmap.", createhashmap_));
    runtime.register_sqfop(unary("createHashMapFromArray", t_array(), "Creates a hashmap from an key-value pair array ([[key, value], ...]).", createhashmapfromarray_array));
    runtime.register_sqfop(binary(4, "set", t_hashmap(), t_array(), "Assigns a value to a hashmap.", set_hashmap_array));
    runtime.register_sqfop(binary(4, "get", t_hashmap(), t_any(), "Receives a value from a hashmap.", get_hashmap_any));
    runtime.register_sqfop(binary(4, "deleteAt", t_hashmap(), t_any(), "Removes a key-value pair from a hashmap.", deleteat_hashmap_any));
    runtime.register_sqfop(binary(4, "in", t_any(), t_hashmap(), "Checks if a given key is inside a value.", in_any_hashmap));
    runtime.register_sqfop(unary("count", t_hashmap(), "Returns the number of elements inside a hashmap.", count_hashmap));
    runtime.register_sqfop(unary("keys", t_hashmap(), "Returns the keys of a hashmap.", keys_hashmap));
    runtime.register_sqfop(binary(4, "getOrDefault", t_hashmap(), t_array(), "Returns the value of a key, or the default when the key is missing. With setDefault, the default is also stored.", getordefault_hashmap_array));
    runtime.register_sqfop(binary(4, "getOrDefaultCall", t_hashmap(), t_array(), "Returns the value of a key, or runs the code when the key is missing and returns its result. With set, the result is also stored.", getordefaultcall_hashmap_array));
    runtime.register_sqfop(binary(4, "merge", t_hashmap(), t_hashmap(), "Adds the pairs of the right HashMap to the left one, keeping existing keys.", merge_hashmap_hashmap));
    runtime.register_sqfop(binary(4, "merge", t_hashmap(), t_array(), "merge [hashMap, overwriteExisting]: adds the pairs of the given HashMap, optionally overwriting existing keys.", merge_hashmap_array));
    runtime.register_sqfop(unary("values", t_hashmap(), "Returns the values of a HashMap.", values_hashmap));
    runtime.register_sqfop(unary("toArray", t_hashmap(), "Returns [[keys], [values]] of a HashMap.", toarray_hashmap));
    runtime.register_sqfop(binary(4, "toArray", t_hashmap(), t_boolean(), "Returns [[keys], [values]] when true, or [[key, value], ...] when false.", toarray_hashmap_boolean));
    runtime.register_sqfop(binary(4, "insert", t_hashmap(), t_array(), "Inserts [[key, value], ...], or [splitArray, keysAndValues], into a HashMap. Existing keys are replaced.", insert_hashmap_array));
    runtime.register_sqfop(binary(4, "createHashMapFromArray", t_array(), t_array(), "Creates a HashMap from an array of keys and an array of values.", createhashmapfromarray_array_array));
}
