#pragma once
#include "ops_config.h"
#include "ops_diag.h"
#include "ops_generic.h"
#include "ops_group.h"
#include "ops_logic.h"
#include "ops_markers.h"
#include "ops_math.h"
#include "ops_namespace.h"
#include "ops_object.h"
#include "ops_sqfvm.h"
#include "ops_string.h"
#include "ops_text.h"
#include "ops_dummy_nular.h"
#include "ops_dummy_unary.h"
#include "ops_dummy_binary.h"
#include "ops_osspecific.h"
#include "ops_hashmap.h"
#include "ops_array.h"
#include "ops_json.h"
#include "ops_loops.h"
#include "ops_scope.h"
#include "ops_regex.h"
#include "ops_nil.h"
#include "ops_system_extra.h"
#include "ops_config_extra.h"
#include "ops_math_extra.h"
#include "ops_hashmap_object.h"
#include "ops_simulated.h"

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }

    namespace operators
    {
        inline void ops(::sqf::runtime::runtime& runtime)
        {
            sqf::operators::ops_config(runtime);
            sqf::operators::ops_diag(runtime);
            sqf::operators::ops_generic(runtime);
            sqf::operators::ops_group(runtime);
            sqf::operators::ops_logic(runtime);
            sqf::operators::ops_markers(runtime);
            sqf::operators::ops_math(runtime);
            sqf::operators::ops_namespace(runtime);
            sqf::operators::ops_object(runtime);
            sqf::operators::ops_sqfvm(runtime);
            sqf::operators::ops_string(runtime);
            sqf::operators::ops_text(runtime);
            sqf::operators::ops_dummy_nular(runtime);
            sqf::operators::ops_dummy_unary(runtime);
            sqf::operators::ops_dummy_binary(runtime);
            sqf::operators::ops_osspecific(runtime);
            sqf::operators::ops_hashmap(runtime);
            sqf::operators::ops_array(runtime);
            sqf::operators::ops_json(runtime);
            sqf::operators::ops_loops(runtime);
            sqf::operators::ops_scope(runtime);
            sqf::operators::ops_regex(runtime);
            sqf::operators::ops_nil(runtime);
            sqf::operators::ops_system_extra(runtime);
            sqf::operators::ops_config_extra(runtime);
            sqf::operators::ops_math_extra(runtime);
            sqf::operators::ops_hashmap_object(runtime);
            // Must stay last: only fills syntaxes that have no real implementation.
            sqf::operators::ops_simulated_support(runtime);
            sqf::operators::ops_simulated(runtime);
        }
    }
}