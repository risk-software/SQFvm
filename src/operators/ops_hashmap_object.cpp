#include "ops_hashmap_object.h"
#include "ops_hashmap.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_code.h"
#include "../runtime/d_string.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    // HashMap object inheritance deeper than this is treated as a cycle.
    constexpr size_t max_inheritance_depth = 64;

    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
    }

    std::string lowercase(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    /// Runs a list of methods one after another in one frame, each with _self and _this set, and
    /// leaves `result` as the frame's value. Used for #create and #clone chains.
    class behavior_method_chain : public frame::behavior
    {
    private:
        std::vector<instruction_set> m_methods;
        size_t m_next;
        value m_self;
        value m_arguments;
        value m_result;
        bool m_seal;
        bool m_no_copy;
    public:
        behavior_method_chain(std::vector<instruction_set> methods, value self, value arguments, value result, bool seal, bool no_copy) :
            m_methods(std::move(methods)), m_next(1), m_self(self), m_arguments(arguments), m_result(result), m_seal(seal), m_no_copy(no_copy) {}

        sqf::runtime::instruction_set get_instruction_set(sqf::runtime::frame& frame) override
        {
            return m_methods[m_next++];
        }
        result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
        {
            runtime.context_active().clear_values();
            if (m_next < m_methods.size())
            {
                frame.clear_value_scope();
                frame["_self"] = m_self;
                frame["_this"] = m_arguments;
                return result::exchange;
            }
            // Flags take effect once construction or cloning is complete.
            auto map = m_self.data<d_hashmap>();
            map->sealed(map->is_sealed() || m_seal);
            map->no_copy(map->is_no_copy() || m_no_copy);
            runtime.context_active().push_value(m_result);
            return result::ok;
        }
    };

    /// Runs the methods with _self and _this, then returns `self`. With no methods, returns `self` directly.
    value run_method_chain(runtime& runtime, const std::vector<value>& methods, value self, value arguments, bool seal, bool no_copy)
    {
        std::vector<instruction_set> code;
        for (auto& method : methods)
        {
            code.push_back(method.data<d_code, instruction_set>());
        }
        if (code.empty())
        {
            auto map = self.data<d_hashmap>();
            map->sealed(map->is_sealed() || seal);
            map->no_copy(map->is_no_copy() || no_copy);
            return self;
        }
        auto first = code.front();
        frame f(runtime.default_value_scope(), first, std::make_shared<behavior_method_chain>(code, self, arguments, self, seal, no_copy));
        f["_self"] = self;
        f["_this"] = arguments;
        runtime.context_active().push_frame(f);
        return {};
    }

    /// A class definition is a HashMap or an array of [key, value] pairs.
    std::shared_ptr<d_hashmap> definition_map(runtime& runtime, value::cref definition)
    {
        if (definition.is<t_hashmap>())
        {
            return definition.data<d_hashmap>();
        }
        if (!definition.is<t_array>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "createHashMapObject", "the class definition must be a HashMap or an array of [key, value] pairs"));
            return {};
        }
        auto map = std::make_shared<d_hashmap>();
        auto pairs = definition.data<d_array>();
        for (size_t i = 0; i < pairs->size(); i++)
        {
            auto pair = pairs->at(i).data_try<d_array>();
            if (!pair || pair->size() != 2)
            {
                runtime.__logmsg(err::ErrorMessage(position(runtime), "createHashMapObject", "the class definition must contain [key, value] pairs"));
                return {};
            }
            map->map()[pair->at(0)] = pair->at(1);
        }
        return map;
    }

    /// Collects the class and its #base classes, base class first.
    bool collect_classes(runtime& runtime, value::cref definition, std::vector<std::shared_ptr<d_hashmap>>& classes)
    {
        if (classes.size() > max_inheritance_depth)
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "createHashMapObject", "#base nesting is too deep or circular"));
            return false;
        }
        auto map = definition_map(runtime, definition);
        if (!map)
        {
            return false;
        }
        auto base = map->map().find(value(std::string("#base")));
        if (base != map->map().end() && !collect_classes(runtime, base->second, classes))
        {
            return false;
        }
        classes.push_back(map);
        return true;
    }

    /// createHashMapObject [classDefinition, constructorArguments] (2.14).
    value createhashmapobject_array(runtime& runtime, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() < 1 || params->size() > 2)
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "createHashMapObject", "expected [classDefinition, constructorArguments]"));
            return {};
        }
        std::vector<std::shared_ptr<d_hashmap>> classes;
        if (!collect_classes(runtime, params->at(0), classes))
        {
            return {};
        }

        auto instance = std::make_shared<d_hashmap>();
        std::vector<value> types;
        std::vector<value> constructors;
        std::vector<value> clone_methods;
        bool seal = false;
        bool no_copy = false;
        for (auto& definition : classes)
        {
            for (auto& pair : definition->map())
            {
                auto key = pair.first.is<t_string>() ? pair.first.data<d_string, std::string>() : std::string();
                if (key == "#base")
                {
                    continue;
                }
                if (key == "#type")
                {
                    if (pair.second.is<t_array>())
                    {
                        auto listed = pair.second.data<d_array>()->value();
                        types.insert(types.begin(), listed.begin(), listed.end());
                    }
                    else
                    {
                        types.insert(types.begin(), pair.second);
                    }
                    continue;
                }
                if (key == "#create" && pair.second.is<t_code>())
                {
                    constructors.push_back(pair.second);
                }
                if (key == "#clone" && pair.second.is<t_code>())
                {
                    clone_methods.push_back(pair.second);
                }
                if (key == "#flags" && pair.second.is<t_array>())
                {
                    auto flags = pair.second.data<d_array>();
                    for (size_t i = 0; i < flags->size(); i++)
                    {
                        auto flag = lowercase(flags->at(i).data_try<d_string, std::string>(std::string()));
                        seal = seal || flag == "sealed";
                        no_copy = no_copy || flag == "nocopy";
                    }
                }
                // Derived classes overwrite base class entries, like merge with overwriteExisting.
                instance->map()[pair.first] = pair.second;
            }
        }
        if (!types.empty())
        {
            instance->map()[value(std::string("#type"))] = value(std::make_shared<d_array>(types));
        }
        instance->clone_methods(clone_methods);
        auto arguments = params->size() == 2 ? params->at(1) : value{};
        return run_method_chain(runtime, constructors, value(instance), arguments, seal, no_copy);
    }

    /// hashMapObject call [methodName, arguments] (2.14). Without arguments, _this is the caller's _this.
    value call_hashmap_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto map = left.data<d_hashmap>();
        auto params = right.data<d_array>();
        if (params->size() < 1 || params->size() > 2 || !params->at(0).is<t_string>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "call", "expected [methodName, arguments]"));
            return {};
        }
        auto method = map->map().find(params->at(0));
        if (method == map->map().end() || !method->second.is<t_code>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "call", "the HashMap has no method " + params->at(0).data<d_string, std::string>()));
            return {};
        }
        value arguments;
        if (params->size() == 2)
        {
            arguments = params->at(1);
        }
        else if (auto caller_this = runtime.context_active().get_variable("_this"); caller_this.has_value())
        {
            arguments = *caller_this;
        }
        frame f(runtime.default_value_scope(), method->second.data<d_code, instruction_set>());
        f["_self"] = left;
        f["_this"] = arguments;
        runtime.context_active().push_frame(f);
        return {};
    }

    value copy_value(value::cref source);

    std::shared_ptr<d_hashmap> copy_hashmap(const std::shared_ptr<d_hashmap>& source)
    {
        auto copy = std::make_shared<d_hashmap>();
        for (auto& pair : source->map())
        {
            copy->map()[pair.first] = copy_value(pair.second);
        }
        copy->clone_methods(source->clone_methods());
        return copy;
    }

    value copy_value(value::cref source)
    {
        if (source.is<t_array>())
        {
            return value(source.data<d_array>()->copy_deep());
        }
        if (source.is<t_hashmap>())
        {
            return value(copy_hashmap(source.data<d_hashmap>()));
        }
        return source;
    }

    /// + hashMap: a deep copy. A copy of a HashMap object runs its #clone methods; noCopy objects cannot be copied.
    value plus_hashmap(runtime& runtime, value::cref right)
    {
        auto source = right.data<d_hashmap>();
        if (source->is_no_copy())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "+", "the HashMap object has the noCopy flag and cannot be copied"));
            return {};
        }
        auto copy = copy_hashmap(source);
        return run_method_chain(runtime, source->clone_methods(), value(copy), value{}, source->is_sealed(), false);
    }

    value compilefinal_string(runtime& runtime, value::cref right)
    {
        auto text = right.data<d_string, std::string>();
        auto compiled = runtime.parser_sqf().parse(runtime, text, position(runtime).path);
        if (!compiled.has_value())
        {
            runtime.__runtime_error() = true;
            return {};
        }
        auto code = std::make_shared<d_code>(*compiled);
        code->make_final();
        return value(code);
    }
    /// compileFinal code (2.14): a final copy of the code.
    value compilefinal_code(runtime& runtime, value::cref right)
    {
        auto code = std::make_shared<d_code>(right.data<d_code, instruction_set>());
        code->make_final();
        return value(code);
    }
    /// compileFinal hashMap (2.14): makes the HashMap itself read-only and returns it.
    value compilefinal_hashmap(runtime& runtime, value::cref right)
    {
        right.data<d_hashmap>()->make_final();
        return right;
    }
    value isfinal_any(runtime& runtime, value::cref right)
    {
        return !right.empty() && right.data<sqf::runtime::data>()->is_final();
    }
}

void sqf::operators::ops_hashmap_object(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(unary("createHashMapObject", t_array(), "createHashMapObject [classDefinition, constructorArguments]: creates a HashMap object, running #create from base to derived class.", createhashmapobject_array));
    runtime.register_sqfop(binary(4, "call", t_hashmap(), t_array(), "hashMapObject call [methodName, arguments]: runs the method with _self set to the HashMap.", call_hashmap_array));
    runtime.register_sqfop(unary("+", t_hashmap(), "Returns a deep copy of the HashMap. HashMap objects run their #clone methods; noCopy objects cannot be copied.", plus_hashmap));
    runtime.register_sqfop(unary("compileFinal", t_string(), "Compiles the string into final code, which cannot be overwritten in a variable.", compilefinal_string));
    runtime.register_sqfop(unary("compileFinal", t_code(), "Returns a final copy of the code.", compilefinal_code));
    runtime.register_sqfop(unary("compileFinal", t_hashmap(), "Makes the HashMap final: it can no longer be changed.", compilefinal_hashmap));
    runtime.register_sqfop(unary("isFinal", t_any(), "Returns true if the code or HashMap was made final with compileFinal.", isfinal_any));
}
