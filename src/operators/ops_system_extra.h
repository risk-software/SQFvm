#pragma once

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        // breakTo, the C-style for loop, fileExists, localize and isLocalized from stringtable.xml,
        // single-machine multiplayer commands and the diagnostic commands that need no game world.
        void ops_system_extra(::sqf::runtime::runtime& runtime);
    }
}
