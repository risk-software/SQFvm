#pragma once
#include "../runtime/value.h"

#include <memory>

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
        /// Sets an object's damage as setDamage does, and fires Killed, MPKilled and EntityKilled when
        /// the object dies.
        void apply_scripted_damage(::sqf::runtime::runtime& runtime, const std::shared_ptr<::sqf::types::object>& target, float damage,
            ::sqf::runtime::value killer, ::sqf::runtime::value instigator, bool use_effects);

        /// The world model: every position form, orientation and model space, properties that scripted
        /// setters store for their getters, crew changes with GetIn and GetOut events, and area and
        /// proximity queries. SQF-VM has a flat world at height 0 without water.
        void ops_world(::sqf::runtime::runtime& runtime);
    }
}
