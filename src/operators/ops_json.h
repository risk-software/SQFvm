#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // toJSON and fromJSON (Arma 3 2.18).
        void ops_json(::sqf::runtime::runtime& runtime);
    }
}
