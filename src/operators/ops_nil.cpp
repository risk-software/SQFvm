#include "ops_nil.h"
#include "ops_generic.h"
#include "../runtime/value.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_code.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    /// nil equals only nil.
    value isequalto_nil(runtime& runtime, value::cref left, value::cref right)
    {
        return left.is<t_nothing>() && right.is<t_nothing>();
    }
    value isnotequalto_nil(runtime& runtime, value::cref left, value::cref right)
    {
        return !(left.is<t_nothing>() && right.is<t_nothing>());
    }
    /// nil has no type, so it matches no type.
    value isequaltype_nil(runtime& runtime, value::cref left, value::cref right)
    {
        return left.is<t_nothing>() && right.is<t_nothing>();
    }
    value str_nil(runtime& runtime, value::cref right)
    {
        return std::string("any");
    }
    /// array pushBack nil appends a nil element and returns its index.
    value pushback_array_nil(runtime& runtime, value::cref left, value::cref right)
    {
        auto arr = left.data<d_array>();
        arr->push_back(value{});
        return static_cast<float>(arr->size() - 1);
    }
    /// nil call code runs the code with _this undefined.
    value call_nil_code(runtime& runtime, value::cref left, value::cref right)
    {
        frame f(runtime.default_value_scope(), right.data<d_code, instruction_set>());
        f["_this"] = value{};
        runtime.context_active().push_frame(f);
        return {};
    }
    value spawn_nil_code(runtime& runtime, value::cref left, value::cref right)
    {
        auto context_weak = runtime.context_create();
        auto lock = context_weak.lock();
        lock->can_suspend(true);
        lock->weak_error_handling(true);
        auto script = std::make_shared<d_script>(context_weak);
        frame f(runtime.default_value_scope(), right.data<d_code, instruction_set>());
        f["_thisScript"] = script;
        f["_this"] = value{};
        lock->push_frame(f);
        return script;
    }
}

void sqf::operators::ops_nil(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    for (auto nil_side : { std::pair<type, type>{ t_nothing(), t_any() }, std::pair<type, type>{ t_any(), t_nothing() }, std::pair<type, type>{ t_nothing(), t_nothing() } })
    {
        runtime.register_sqfop(binary(4, "isEqualTo", nil_side.first, nil_side.second, "nil is equal only to nil.", isequalto_nil));
        runtime.register_sqfop(binary(4, "isNotEqualTo", nil_side.first, nil_side.second, "nil is equal only to nil.", isnotequalto_nil));
        runtime.register_sqfop(binary(4, "isEqualType", nil_side.first, nil_side.second, "nil matches no type.", isequaltype_nil));
    }
    runtime.register_sqfop(unary("str", t_nothing(), "str nil is \"any\".", str_nil));
    runtime.register_sqfop(binary(4, "pushBack", t_array(), t_nothing(), "Appends a nil element.", pushback_array_nil));
    runtime.register_sqfop(binary(4, "call", t_nothing(), t_code(), "Runs the code with _this undefined.", call_nil_code));
    runtime.register_sqfop(binary(4, "spawn", t_nothing(), t_code(), "Runs the code scheduled with _this undefined.", spawn_nil_code));
}
