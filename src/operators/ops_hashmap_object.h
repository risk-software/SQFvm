#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // createHashMapObject and hashMapObject call [method, arguments] (Arma 3 2.14), the + copy of
        // HashMaps with #clone and #flags, and compileFinal / isFinal for code and HashMaps.
        void ops_hashmap_object(::sqf::runtime::runtime& runtime);
    }
}
