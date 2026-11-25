#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // Loop control (break, breakWith, continue, continueWith), forEachReversed, HashMap iteration
        // and the Arma 3 2.22 waitUntil syntaxes.
        void ops_loops(::sqf::runtime::runtime& runtime);
    }
}
