#include "ops_array.h"
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
#include <random>
#include <set>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
    }

    /// Copies arrays and HashMaps, as the engine does when one value fills several slots.
    value copy_value(value::cref source)
    {
        if (source.is<t_array>())
        {
            return value(source.data<d_array>()->copy_deep());
        }
        if (source.is<t_hashmap>())
        {
            return value(std::make_shared<d_hashmap>(source.data<d_hashmap>()->map()));
        }
        return source;
    }

    void flatten_into(std::shared_ptr<d_array> source, std::vector<value>& out)
    {
        for (size_t i = 0; i < source->size(); i++)
        {
            auto& element = source->at(i);
            if (element.is<t_array>())
            {
                flatten_into(element.data<d_array>(), out);
            }
            else
            {
                out.push_back(element);
            }
        }
    }
    value flatten_array(runtime& runtime, value::cref right)
    {
        std::vector<value> out;
        flatten_into(right.data<d_array>(), out);
        return std::make_shared<d_array>(out);
    }

    value findany_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto search_in = left.data<d_array>();
        auto to_find = right.data<d_array>();
        for (size_t i = 0; i < search_in->size(); i++)
        {
            for (size_t j = 0; j < to_find->size(); j++)
            {
                if (search_in->at(i) == to_find->at(j))
                {
                    return static_cast<float>(i);
                }
            }
        }
        return -1.0f;
    }

    /// array deleteAt [indexes] (2.18): deletes several elements, negative indexes count from the end.
    /// Returns the deleted elements in the order of the given indexes.
    value deleteat_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto arr = left.data<d_array>();
        auto indexes = right.data<d_array>();
        auto size = static_cast<long>(arr->size());
        std::vector<value> deleted;
        std::set<long, std::greater<long>> to_delete;
        for (size_t i = 0; i < indexes->size(); i++)
        {
            if (!indexes->at(i).is<t_scalar>())
            {
                runtime.__logmsg(err::ExpectedArrayTypeMissmatch(position(runtime), i, t_scalar(), indexes->at(i).type()));
                return {};
            }
            auto index = static_cast<long>(indexes->at(i).data<d_scalar, float>());
            if (index < 0)
            {
                index += size;
            }
            if (index < 0 || index >= size)
            {
                runtime.__logmsg(err::IndexOutOfRangeWeak(position(runtime), arr->size(), index));
                continue;
            }
            deleted.push_back(arr->at(index));
            to_delete.insert(index);
        }
        // Deleting from the back keeps the remaining indexes valid.
        for (auto index : to_delete)
        {
            arr->erase(arr->begin() + index);
        }
        return std::make_shared<d_array>(deleted);
    }

    /// array resize [count, fill] (2.10): new places get a copy of the fill value.
    value resize_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto arr = left.data<d_array>();
        auto params = right.data<d_array>();
        if (params->size() != 2 || !params->at(0).is<t_scalar>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "resize", "expected [count, fill]"));
            return {};
        }
        auto count = params->at(0).data<d_scalar, float>();
        if (count < 0)
        {
            runtime.__logmsg(err::NegativeSize(position(runtime)));
            return {};
        }
        auto old_size = arr->size();
        arr->resize(static_cast<size_t>(count));
        for (auto i = old_size; i < arr->size(); i++)
        {
            arr->at(i) = copy_value(params->at(1));
        }
        return {};
    }

    /// array insert [index, values, onlyIfUnique] (2.02). -1 appends, -2 inserts before the last element (2.14).
    value insert_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto arr = left.data<d_array>();
        auto params = right.data<d_array>();
        if (params->size() < 2 || params->size() > 3 || !params->at(0).is<t_scalar>() || !params->at(1).is<t_array>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "insert", "expected [index, values, onlyIfUnique]"));
            return {};
        }
        auto size = static_cast<long>(arr->size());
        auto index = static_cast<long>(params->at(0).data<d_scalar, float>());
        if (index < 0)
        {
            index = size + index + 1;
        }
        index = std::clamp(index, 0L, size);
        bool only_if_unique = params->size() == 3 && params->at(2).data_try<d_boolean, bool>(false);
        auto values = params->at(1).data<d_array>();
        for (size_t i = 0; i < values->size(); i++)
        {
            auto& candidate = values->at(i);
            if (only_if_unique && std::find(arr->begin(), arr->end(), candidate) != arr->end())
            {
                continue;
            }
            arr->value().insert(arr->value().begin() + index, candidate);
            index++;
        }
        return {};
    }

    /// string insert [index, substring] (2.02). -1 appends.
    value insert_string_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto text = left.data<d_string, std::string>();
        auto params = right.data<d_array>();
        if (params->size() != 2 || !params->at(0).is<t_scalar>() || !params->at(1).is<t_string>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "insert", "expected [index, substring]"));
            return text;
        }
        auto size = static_cast<long>(text.size());
        auto index = static_cast<long>(params->at(0).data<d_scalar, float>());
        if (index < 0)
        {
            index = size + index + 1;
        }
        index = std::clamp(index, 0L, size);
        text.insert(static_cast<size_t>(index), params->at(1).data<d_string, std::string>());
        return text;
    }

    value select_weighted(runtime& runtime, const std::vector<value>& values, const std::vector<value>& weights)
    {
        auto count = std::min(values.size(), weights.size());
        float total = 0;
        for (size_t i = 0; i < count; i++)
        {
            auto weight = weights[i].data_try<d_scalar, float>(0.0f);
            if (weight > 0)
            {
                total += weight;
            }
        }
        if (total <= 0)
        {
            return {};
        }
        static std::mt19937 generator(std::random_device{}());
        std::uniform_real_distribution<float> distribution(0.0f, total);
        auto pick = distribution(generator);
        for (size_t i = 0; i < count; i++)
        {
            auto weight = weights[i].data_try<d_scalar, float>(0.0f);
            if (weight <= 0)
            {
                continue;
            }
            if (pick < weight)
            {
                return values[i];
            }
            pick -= weight;
        }
        return values[count - 1];
    }
    /// selectRandomWeighted [value1, weight1, value2, weight2, ...]
    value selectrandomweighted_array(runtime& runtime, value::cref right)
    {
        auto arr = right.data<d_array>();
        std::vector<value> values;
        std::vector<value> weights;
        for (size_t i = 0; i + 1 < arr->size(); i += 2)
        {
            values.push_back(arr->at(i));
            weights.push_back(arr->at(i + 1));
        }
        return select_weighted(runtime, values, weights);
    }
    value selectrandomweighted_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        return select_weighted(runtime, left.data<d_array>()->value(), right.data<d_array>()->value());
    }

    /// Same instance for arrays, HashMaps and other reference types.
    bool same_reference(value::cref left, value::cref right)
    {
        return left.data<sqf::runtime::data>() == right.data<sqf::runtime::data>();
    }
    value isequalref_any_any(runtime& runtime, value::cref left, value::cref right)
    {
        return same_reference(left, right);
    }
    value isnotequalref_any_any(runtime& runtime, value::cref left, value::cref right)
    {
        return !same_reference(left, right);
    }

    value reverse_string(runtime& runtime, value::cref right)
    {
        auto text = right.data<d_string, std::string>();
        std::reverse(text.begin(), text.end());
        return text;
    }

    /// string find [substring, startIndex] (2.02).
    value find_string_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto text = left.data<d_string, std::string>();
        auto params = right.data<d_array>();
        if (params->size() != 2 || !params->at(0).is<t_string>() || !params->at(1).is<t_scalar>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "find", "expected [substring, startIndex]"));
            return -1.0f;
        }
        auto start = static_cast<long>(params->at(1).data<d_scalar, float>());
        if (start < 0 || start > static_cast<long>(text.size()))
        {
            return -1.0f;
        }
        auto found = text.find(params->at(0).data<d_string, std::string>(), static_cast<size_t>(start));
        return found == std::string::npos ? -1.0f : static_cast<float>(found);
    }
}

void sqf::operators::ops_array(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(unary("flatten", t_array(), "Returns a copy of the array with all nested arrays flattened into it.", flatten_array));
    runtime.register_sqfop(binary(4, "findAny", t_array(), t_array(), "Returns the index of the first element of the left array that is in the right array, or -1.", findany_array_array));
    runtime.register_sqfop(binary(4, "deleteAt", t_array(), t_array(), "Deletes the elements at the given indexes (negative indexes count from the end) and returns them.", deleteat_array_array));
    runtime.register_sqfop(binary(4, "resize", t_array(), t_array(), "resize [count, fill]: resizes the array and fills new places with copies of the fill value.", resize_array_array));
    runtime.register_sqfop(binary(4, "insert", t_array(), t_array(), "insert [index, values, onlyIfUnique]: inserts values at the index. -1 appends, -2 inserts before the last element.", insert_array_array));
    runtime.register_sqfop(binary(4, "insert", t_string(), t_array(), "insert [index, substring]: returns the string with the substring inserted at the index. -1 appends.", insert_string_array));
    runtime.register_sqfop(unary("selectRandomWeighted", t_array(), "Returns a random element of [value1, weight1, value2, weight2, ...] according to the weights.", selectrandomweighted_array));
    runtime.register_sqfop(binary(4, "selectRandomWeighted", t_array(), t_array(), "Returns a random element of the values according to the weights.", selectrandomweighted_array_array));
    runtime.register_sqfop(binary(4, "isEqualRef", t_any(), t_any(), "Returns true if both values are the same instance.", isequalref_any_any));
    runtime.register_sqfop(binary(4, "isNotEqualRef", t_any(), t_any(), "Returns true if the values are different instances.", isnotequalref_any_any));
    runtime.register_sqfop(unary("reverse", t_string(), "Returns the string reversed.", reverse_string));
    runtime.register_sqfop(binary(4, "find", t_string(), t_array(), "find [substring, startIndex]: returns the index of the substring from the start index, or -1.", find_string_array));
}
