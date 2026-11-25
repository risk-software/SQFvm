#pragma once
#include "../runtime/value.h"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }
    namespace types
    {
        class object;
    }

    namespace operators
    {
        /// Runs the object's handlers for the event now, unscheduled, in the active script, with
        /// _this = params, _thisEvent and _thisEventHandler set. MP handlers of the same event run too.
        void fire_object_event(::sqf::runtime::runtime& runtime, const std::shared_ptr<::sqf::types::object>& object,
            std::string_view event, std::vector<::sqf::runtime::value> params);

        /// Runs an object event and the matching mission event (for example Killed and EntityKilled), then
        /// `after`, once the last handler is done. Without handlers, `after` runs at once.
        void fire_object_and_mission_event(::sqf::runtime::runtime& runtime, const std::shared_ptr<::sqf::types::object>& object,
            std::string_view object_event, std::vector<::sqf::runtime::value> object_params,
            std::string_view mission_event, std::vector<::sqf::runtime::value> mission_params,
            std::function<void()> after = {});

        /// Runs the mission event handlers for the event, unscheduled, with _thisArgs set as well. With
        /// deferred, they run in their own script right after the current one yields; commands that return
        /// a value use that, so the value is not taken by the handlers' frames.
        void fire_mission_event(::sqf::runtime::runtime& runtime, std::string_view event, std::vector<::sqf::runtime::value> params, bool deferred = false);

        /// addEventHandler, addMPEventHandler, addMissionEventHandler and their remove commands, and the
        /// EachFrame mission event, which runs once per pass over all scripts.
        void ops_world_events(::sqf::runtime::runtime& runtime);
    }
}
