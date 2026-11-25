#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // Mission and campaign config roots, getMissionConfigValue, getTextRaw and the Arma 3 2.22
        // array forms of configClasses and configHierarchy.
        void ops_config_extra(::sqf::runtime::runtime& runtime);
    }
}
