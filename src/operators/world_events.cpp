#include "world_events.h"
#include "object.h"
#include "d_object.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_code.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    // MP handlers are stored with this prefix next to the normal ones. SQF-VM is one machine, so an
    // MP event runs exactly where the normal event does.
    const std::string mp_prefix = "mp:";
    const std::string each_frame_context_name = "EachFrame";

    std::string lowercase(std::string_view text)
    {
        std::string out(text);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
    }

    struct mission_handler
    {
        int id;
        value code;
        value arguments;
    };

    class mission_event_storage : public sqf::runtime::runtime::datastorage
    {
    public:
        std::unordered_map<std::string, std::vector<mission_handler>> handlers;
        std::unordered_map<std::string, int> next_id;
        ~mission_event_storage() override {}
    };

    struct pending_handler
    {
        value code;
        value params;
        std::string event;
        int id;
        value arguments;
    };

    /// Restores the script's scheduling state after the last handler of an event has run.
    class behavior_restore_suspend : public frame::behavior
    {
    private:
        bool m_could_suspend;
        std::function<void()> m_after;
    public:
        behavior_restore_suspend(bool could_suspend, std::function<void()> after) : m_could_suspend(could_suspend), m_after(std::move(after)) {}
        result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
        {
            runtime.context_active().can_suspend(m_could_suspend);
            if (m_after)
            {
                m_after();
            }
            return result::ok;
        }
    };

    /// Compiles string handlers, as the engine accepts code or a string.
    std::optional<instruction_set> handler_code(runtime& runtime, value::cref code)
    {
        if (code.is<t_code>())
        {
            return code.data<d_code, instruction_set>();
        }
        if (code.is<t_string>())
        {
            auto parsed = runtime.parser_sqf().parse(runtime, code.data<d_string, std::string>(), position(runtime).path);
            if (parsed.has_value())
            {
                return *parsed;
            }
        }
        return {};
    }

    /// Pushes the handlers so they run one after another, first added first, unscheduled.
    void run_handlers(runtime& runtime, context& target, std::vector<pending_handler> handlers, std::function<void()> after = {})
    {
        // Handlers that cannot be compiled are skipped, so `after` must hang on a handler that runs.
        handlers.erase(std::remove_if(handlers.begin(), handlers.end(), [&](const pending_handler& h) { return !handler_code(runtime, h.code); }), handlers.end());
        if (handlers.empty())
        {
            if (after)
            {
                after();
            }
            return;
        }
        bool could_suspend = target.can_suspend();
        target.can_suspend(false);
        // Frames run last-pushed first, so the last handler is pushed first and restores scheduling.
        for (size_t i = handlers.size(); i-- > 0;)
        {
            auto& handler = handlers[i];
            auto code = handler_code(runtime, handler.code);
            if (!code)
            {
                continue;
            }
            std::shared_ptr<frame::behavior> restore;
            if (i == handlers.size() - 1)
            {
                restore = std::make_shared<behavior_restore_suspend>(could_suspend, after);
            }
            frame f(runtime.default_value_scope(), *code, restore);
            f["_this"] = handler.params;
            f["_thisEvent"] = handler.event;
            f["_thisEventHandler"] = static_cast<float>(handler.id);
            f["_thisArgs"] = handler.arguments;
            target.push_frame(f);
        }
    }

    value add_object_handler(runtime& runtime, value::cref left, value::cref right, const std::string& prefix)
    {
        auto target = left.data<d_object>();
        auto params = right.data<d_array>();
        if (target->is_null() || params->size() != 2 || !params->at(0).is<t_string>() || !(params->at(1).is<t_code>() || params->at(1).is<t_string>()))
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "addEventHandler", "expected a non-null object and [type, code]"));
            return -1.0f;
        }
        auto event = prefix + lowercase(params->at(0).data<d_string, std::string>());
        auto& world = target->value()->world();
        auto id = world.next_event_handler_id[event]++;
        world.event_handlers[event].push_back({ id, params->at(1) });
        return static_cast<float>(id);
    }
    value remove_object_handler(runtime& runtime, value::cref left, value::cref right, const std::string& prefix)
    {
        auto target = left.data<d_object>();
        auto params = right.data<d_array>();
        if (target->is_null() || params->size() != 2 || !params->at(0).is<t_string>() || !params->at(1).is<t_scalar>())
        {
            return {};
        }
        auto& handlers = target->value()->world().event_handlers[prefix + lowercase(params->at(0).data<d_string, std::string>())];
        auto id = static_cast<int>(params->at(1).data<d_scalar, float>());
        handlers.erase(std::remove_if(handlers.begin(), handlers.end(), [id](const object::world_state::event_handler& h) { return h.id == id; }), handlers.end());
        return {};
    }
    value remove_all_object_handlers(runtime& runtime, value::cref left, value::cref right, const std::string& prefix)
    {
        auto target = left.data<d_object>();
        if (!target->is_null())
        {
            target->value()->world().event_handlers[prefix + lowercase(right.data<d_string, std::string>())].clear();
        }
        return {};
    }

    /// addMissionEventHandler [type, code, arguments]: arguments are available as _thisArgs (2.04).
    value addmissioneventhandler_array(runtime& runtime, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() < 2 || params->size() > 3 || !params->at(0).is<t_string>() || !(params->at(1).is<t_code>() || params->at(1).is<t_string>()))
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "addMissionEventHandler", "expected [type, code, arguments]"));
            return -1.0f;
        }
        auto& storage = runtime.storage<mission_event_storage>();
        auto event = lowercase(params->at(0).data<d_string, std::string>());
        auto id = storage.next_id[event]++;
        storage.handlers[event].push_back({ id, params->at(1), params->size() == 3 ? params->at(2) : value(std::make_shared<d_array>()) });
        return static_cast<float>(id);
    }
    value removemissioneventhandler_array(runtime& runtime, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() != 2 || !params->at(0).is<t_string>() || !params->at(1).is<t_scalar>())
        {
            return {};
        }
        auto& handlers = runtime.storage<mission_event_storage>().handlers[lowercase(params->at(0).data<d_string, std::string>())];
        auto id = static_cast<int>(params->at(1).data<d_scalar, float>());
        handlers.erase(std::remove_if(handlers.begin(), handlers.end(), [id](const mission_handler& h) { return h.id == id; }), handlers.end());
        return {};
    }
    value removeallmissioneventhandlers_string(runtime& runtime, value::cref right)
    {
        runtime.storage<mission_event_storage>().handlers[lowercase(right.data<d_string, std::string>())].clear();
        return {};
    }

    std::vector<pending_handler> mission_handlers(runtime& runtime, std::string_view event, const std::vector<value>& params)
    {
        std::vector<pending_handler> out;
        auto& storage = runtime.storage<mission_event_storage>();
        auto found = storage.handlers.find(lowercase(event));
        if (found == storage.handlers.end())
        {
            return out;
        }
        auto params_value = value(std::make_shared<d_array>(params));
        for (auto& handler : found->second)
        {
            out.push_back({ handler.code, params_value, std::string(event), handler.id, handler.arguments });
        }
        return out;
    }
}

namespace
{
    std::vector<pending_handler> object_handlers(const std::shared_ptr<object>& target, std::string_view event, const std::vector<value>& params)
    {
        std::vector<pending_handler> pending;
        auto params_value = value(std::make_shared<d_array>(params));
        auto& handlers = target->world().event_handlers;
        for (auto& key : { lowercase(event), mp_prefix + lowercase(event) })
        {
            auto found = handlers.find(key);
            if (found == handlers.end())
            {
                continue;
            }
            for (auto& handler : found->second)
            {
                pending.push_back({ handler.code, params_value, std::string(event), handler.id, value{} });
            }
        }
        return pending;
    }
}

void sqf::operators::fire_object_event(::sqf::runtime::runtime& runtime, const std::shared_ptr<::sqf::types::object>& target,
    std::string_view event, std::vector<::sqf::runtime::value> params)
{
    run_handlers(runtime, runtime.context_active(), object_handlers(target, event, params));
}

void sqf::operators::fire_object_and_mission_event(::sqf::runtime::runtime& runtime, const std::shared_ptr<::sqf::types::object>& target,
    std::string_view object_event, std::vector<::sqf::runtime::value> object_params,
    std::string_view mission_event, std::vector<::sqf::runtime::value> mission_params,
    std::function<void()> after)
{
    auto pending = object_handlers(target, object_event, object_params);
    auto mission = mission_handlers(runtime, mission_event, mission_params);
    pending.insert(pending.end(), mission.begin(), mission.end());
    run_handlers(runtime, runtime.context_active(), pending, std::move(after));
}

void sqf::operators::fire_mission_event(::sqf::runtime::runtime& runtime, std::string_view event, std::vector<::sqf::runtime::value> params, bool deferred)
{
    auto pending = mission_handlers(runtime, event, params);
    if (pending.empty())
    {
        return;
    }
    if (deferred)
    {
        auto handler_context = runtime.context_create().lock();
        handler_context->name(std::string(event));
        run_handlers(runtime, *handler_context, pending);
        return;
    }
    run_handlers(runtime, runtime.context_active(), pending);
}

void sqf::operators::ops_world_events(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(binary(4, "addEventHandler", t_object(), t_array(), "object addEventHandler [type, code]: returns the handler id.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return add_object_handler(runtime, left, right, ""); }));
    runtime.register_sqfop(binary(4, "removeEventHandler", t_object(), t_array(), "object removeEventHandler [type, id]",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return remove_object_handler(runtime, left, right, ""); }));
    runtime.register_sqfop(binary(4, "removeAllEventHandlers", t_object(), t_string(), "Removes every handler of the event type.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return remove_all_object_handlers(runtime, left, right, ""); }));
    runtime.register_sqfop(binary(4, "addMPEventHandler", t_object(), t_array(), "object addMPEventHandler [type, code]: SQF-VM is one machine, so it runs with the normal event.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return add_object_handler(runtime, left, right, mp_prefix); }));
    runtime.register_sqfop(binary(4, "removeMPEventHandler", t_object(), t_array(), "object removeMPEventHandler [type, id]",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return remove_object_handler(runtime, left, right, mp_prefix); }));
    runtime.register_sqfop(binary(4, "removeAllMPEventHandlers", t_object(), t_string(), "Removes every MP handler of the event type.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return remove_all_object_handlers(runtime, left, right, mp_prefix); }));
    runtime.register_sqfop(unary("addMissionEventHandler", t_array(), "addMissionEventHandler [type, code, arguments]: returns the handler id.", addmissioneventhandler_array));
    runtime.register_sqfop(unary("removeMissionEventHandler", t_array(), "removeMissionEventHandler [type, id]", removemissioneventhandler_array));
    runtime.register_sqfop(unary("removeAllMissionEventHandlers", t_string(), "Removes every mission handler of the event type.", removeallmissioneventhandlers_string));

    // EachFrame handlers run in their own unscheduled script once per pass over all scripts. They only
    // run while another script is running, so SQF-VM still ends when the tested scripts are done.
    runtime.add_frame_callback([](::sqf::runtime::runtime& runtime)
    {
        auto pending = mission_handlers(runtime, "EachFrame", {});
        if (pending.empty())
        {
            return;
        }
        bool other_scripts = false;
        for (auto it = runtime.context_begin(); it != runtime.context_end(); ++it)
        {
            if ((*it)->name() != each_frame_context_name && !(*it)->empty())
            {
                other_scripts = true;
                break;
            }
        }
        if (!other_scripts)
        {
            return;
        }
        auto frame_context = runtime.context_create().lock();
        frame_context->name(each_frame_context_name);
        run_handlers(runtime, *frame_context, pending);
    });
}
