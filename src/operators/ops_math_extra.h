#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // Math, vector, random, hashing, time and text commands that need no game world.
        void ops_math_extra(::sqf::runtime::runtime& runtime);
    }
}
