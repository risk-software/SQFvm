#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // Scope and namespace commands added in Arma 3 2.x: privateAll, import, namespace isNil,
        // localNamespace, missionProfileNamespace, serverNamespace and unary spawn.
        void ops_scope(::sqf::runtime::runtime& runtime);
    }
}
