#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // Syntaxes that accept nil arguments, as in the engine (isEqualTo, isEqualType, str nil,
        // pushBack nil, nil call, nil spawn). Every other command given nil evaluates to nil.
        void ops_nil(::sqf::runtime::runtime& runtime);
    }
}
