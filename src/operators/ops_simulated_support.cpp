#include "ops_simulated.h"
#include "ops_hashmap.h"
#include "ops_generic.h"
#include "ops_namespace.h"
#include "d_handle.h"
#include "d_object.h"
#include "d_group.h"
#include "d_side.h"
#include "d_text.h"
#include "d_config.h"
#include "../runtime/runtime.h"
#include "../runtime/logging.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_code.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_set>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    value array_of_zeros(size_t count)
    {
        std::vector<value> zeros(count, value(0.0f));
        return std::make_shared<d_array>(zeros);
    }

    value null_handle(sqf::runtime::type type)
    {
        return value(std::make_shared<d_handle>(type));
    }

    template<typename THandleType>
    void register_handle_type(runtime& runtime, const char* null_name)
    {
        using namespace sqf::runtime::sqfop;
        std::string key_name(null_name);
        std::transform(key_name.begin(), key_name.end(), key_name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!runtime.sqfop_exists(sqfop_nular::key{ key_name }))
        {
            runtime.register_sqfop(nular(null_name, "A null value of this type.",
                [](::sqf::runtime::runtime& runtime) -> value { return null_handle(THandleType()); }));
        }
        if (!runtime.sqfop_exists(sqfop_unary::key{ "isnull", THandleType() }))
        {
            runtime.register_sqfop(unary("isNull", THandleType(), "Checks whether the value is the null value of its type.",
                [](::sqf::runtime::runtime& runtime, value::cref right) -> value
                {
                    auto handle = right.data_try<d_handle>();
                    return !handle || handle->is_null();
                }));
        }
    }
}

value sqf::operators::simulated_result(::sqf::runtime::runtime& runtime, std::string_view command, simulated_return type)
{
    static std::mutex reported_mutex;
    static std::unordered_set<std::string> reported;
    {
        std::lock_guard<std::mutex> lock(reported_mutex);
        if (reported.insert(std::string(command)).second)
        {
            runtime.__logmsg(err::WarningMessage(runtime.context_active().current_frame().diag_info_from_position(),
                "SIMULATED", std::string(command) + " needs the game world; SQF-VM returns a default value"));
        }
    }
    switch (type)
    {
    case simulated_return::nothing: return {};
    case simulated_return::boolean: return false;
    case simulated_return::number: return 0.0f;
    case simulated_return::string: return std::string();
    case simulated_return::array: return value(std::make_shared<d_array>());
    case simulated_return::color: return array_of_zeros(4);
    case simulated_return::position: return array_of_zeros(3);
    case simulated_return::object: return value(std::make_shared<d_object>());
    case simulated_return::group: return value(std::make_shared<d_group>());
    case simulated_return::side: return value(std::make_shared<d_side>(d_side::side::Unknown));
    case simulated_return::config: return value(sqf::runtime::config());
    case simulated_return::text: return value(std::make_shared<d_text>());
    case simulated_return::hashmap: return value(std::make_shared<d_hashmap>());
    case simulated_return::display: return null_handle(t_display());
    case simulated_return::control: return null_handle(t_control());
    case simulated_return::task: return null_handle(t_task());
    case simulated_return::location: return null_handle(t_location());
    case simulated_return::teammember: return null_handle(t_teammember());
    case simulated_return::diaryrecord: return null_handle(t_diaryrecord());
    case simulated_return::script: return value(std::make_shared<d_script>());
    case simulated_return::namespace_: return value(std::make_shared<d_namespace>(runtime.get_value_scope(sqf::runtime::missionNamespace)));
    case simulated_return::code: return value(std::make_shared<d_code>());
    }
    return {};
}

void sqf::operators::ops_simulated_support(::sqf::runtime::runtime& runtime)
{
    register_handle_type<t_location>(runtime, "locationNull");
    register_handle_type<t_display>(runtime, "displayNull");
    register_handle_type<t_control>(runtime, "controlNull");
    register_handle_type<t_task>(runtime, "taskNull");
    register_handle_type<t_teammember>(runtime, "teamMemberNull");
    register_handle_type<t_diaryrecord>(runtime, "diaryRecordNull");
    register_handle_type<t_netobject>(runtime, "netObjNull");
}
