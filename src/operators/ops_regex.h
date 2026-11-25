#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // regexMatch, regexFind and regexReplace (Arma 3 2.06), using Boost.Regex as the engine does.
        void ops_regex(::sqf::runtime::runtime& runtime);
    }
}
