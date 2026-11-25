#include "ops_scope.h"
#include "ops_generic.h"
#include "ops_namespace.h"
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

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    const std::string local_namespace_name = "localNamespace";
    const std::string mission_profile_namespace_name = "missionProfileNamespace";
    const std::string server_namespace_name = "serverNamespace";

    std::string lowercase(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    /// privateAll (2.18): local variables of parent scopes can no longer be read or changed.
    value privateall_(runtime& runtime)
    {
        runtime.context_active().current_frame().bubble_variable(false);
        return {};
    }

    /// Copies a local variable of a parent scope into the current scope, even after privateAll.
    bool import_variable(runtime& runtime, std::string name)
    {
        auto& context = runtime.context_active();
        name = lowercase(name);
        if (name.empty() || name[0] != '_')
        {
            runtime.__logmsg(err::ErrorMessage(context.current_frame().diag_info_from_position(), "import", "only local variables can be imported: " + name));
            return false;
        }
        if (context.current_frame().contains(name))
        {
            runtime.__logmsg(err::ErrorMessage(context.current_frame().diag_info_from_position(), "import", "variable already defined in the current scope: " + name));
            return false;
        }
        auto it = context.frames_rbegin();
        ++it; // the current scope was checked above
        for (; it != context.frames_rend(); ++it)
        {
            if (it->contains(name))
            {
                context.current_frame()[name] = (*it)[name];
                return true;
            }
            if (!it->bubble_variable())
            {
                break;
            }
        }
        runtime.__logmsg(err::ErrorMessage(context.current_frame().diag_info_from_position(), "import", "variable not found in a parent scope: " + name));
        return false;
    }
    value import_string(runtime& runtime, value::cref right)
    {
        import_variable(runtime, right.data<d_string, std::string>());
        return {};
    }
    value import_array(runtime& runtime, value::cref right)
    {
        auto names = right.data<d_array>();
        for (size_t i = 0; i < names->size(); i++)
        {
            if (!names->at(i).is<t_string>())
            {
                runtime.__logmsg(err::ExpectedArrayTypeMissmatch(runtime.context_active().current_frame().diag_info_from_position(), i, t_string(), names->at(i).type()));
                continue;
            }
            import_variable(runtime, names->at(i).data<d_string, std::string>());
        }
        return {};
    }

    /// namespace isNil "name" (2.18).
    value isnil_namespace_string(runtime& runtime, value::cref left, value::cref right)
    {
        std::shared_ptr<value_scope> scope = *left.data<d_namespace>();
        if (!scope)
        {
            return true;
        }
        auto found = scope->try_get(right.data<d_string, std::string>());
        return !found.has_value() || found->empty() || found->is<t_nothing>();
    }

    value namespace_value(runtime& runtime, const std::string& name)
    {
        return std::make_shared<d_namespace>(runtime.get_value_scope(name));
    }

    /// spawn code (2.22): runs the code scheduled with nil as _this.
    value spawn_code(runtime& runtime, value::cref right)
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

void sqf::operators::ops_scope(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(nular("privateAll", "Stops the current scope from reading or changing local variables of parent scopes.", privateall_));
    runtime.register_sqfop(unary("import", t_string(), "Copies a local variable of a parent scope into the current scope.", import_string));
    runtime.register_sqfop(unary("import", t_array(), "Copies local variables of parent scopes into the current scope.", import_array));
    runtime.register_sqfop(binary(4, "isNil", t_namespace(), t_string(), "Returns true if the variable is not defined in the namespace.", isnil_namespace_string));
    runtime.register_sqfop(nular("localNamespace", "Returns the namespace that is local to this machine and mission.",
        [](::sqf::runtime::runtime& runtime) -> value { return namespace_value(runtime, local_namespace_name); }));
    runtime.register_sqfop(nular("missionProfileNamespace", "Returns the mission's profile namespace. SQF-VM keeps it in memory only.",
        [](::sqf::runtime::runtime& runtime) -> value { return namespace_value(runtime, mission_profile_namespace_name); }));
    runtime.register_sqfop(nular("serverNamespace", "Returns the server namespace. SQF-VM runs as the server, so it is always available.",
        [](::sqf::runtime::runtime& runtime) -> value { return namespace_value(runtime, server_namespace_name); }));
    runtime.register_sqfop(nular("isMissionProfileNamespaceLoaded", "Returns true; SQF-VM keeps the mission profile namespace in memory.",
        [](::sqf::runtime::runtime& runtime) -> value { return true; }));
    runtime.register_sqfop(nular("saveMissionProfileNamespace", "Accepted for compatibility; SQF-VM does not write profile files. Returns true.",
        [](::sqf::runtime::runtime& runtime) -> value { return true; }));
    runtime.register_sqfop(nular("saveProfileNamespace", "Accepted for compatibility; SQF-VM does not write profile files.",
        [](::sqf::runtime::runtime& runtime) -> value { return {}; }));
    runtime.register_sqfop(unary("spawn", t_code(), "Runs the code scheduled, with nil as _this, and returns its script handle.", spawn_code));
}
