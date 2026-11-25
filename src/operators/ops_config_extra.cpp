#include "ops_config_extra.h"
#include "d_config.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <algorithm>
#include <string>
#include <vector>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    // SQF-VM has one config tree. Mission (description.ext) and campaign configs are read from
    // these top-level classes of the loaded config, so a test can provide them with --config.
    const std::string mission_config_class = "MissionConfigFile";
    const std::string campaign_config_class = "CampaignConfigFile";

    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
    }

    bool is_class(const confignav& nav)
    {
        return !nav.empty() && (nav->size() > 0 || nav->value.empty());
    }

    value child_class_root(runtime& runtime, const std::string& name)
    {
        auto nav = runtime.confighost().root() / name;
        if (nav.empty())
        {
            return config();
        }
        return { *nav };
    }

    /// The class and its logical parents, from the root down.
    std::vector<confignav> logical_path(confignav nav)
    {
        std::vector<confignav> path;
        while (!nav.empty())
        {
            path.push_back(nav);
            nav = nav.parent_logical();
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    value config_or_name(const confignav& nav, bool names)
    {
        if (names)
        {
            return std::string(nav->name);
        }
        return { *nav };
    }

    /// configHierarchy [config, classesOnly, includeBases, reversedOrder, configNames] (2.22).
    /// includeBases follows inheritance (like BIS_fnc_returnParents) instead of the logical path.
    value confighierarchy_array(runtime& runtime, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() < 4 || params->size() > 5 || !params->at(0).is<t_config>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "configHierarchy", "expected [config, classesOnly, includeBases, reversedOrder, configNames]"));
            return std::make_shared<d_array>();
        }
        auto nav = params->at(0).data<d_config, config>().navigate(runtime.confighost());
        bool classes_only = params->at(1).data_try<d_boolean, bool>(false);
        bool include_bases = params->at(2).data_try<d_boolean, bool>(false);
        bool reversed = params->at(3).data_try<d_boolean, bool>(false);
        bool names = params->size() == 5 && params->at(4).data_try<d_boolean, bool>(false);
        std::vector<confignav> chain;
        if (include_bases)
        {
            for (auto entry = nav; !entry.empty(); entry = entry.parent_inherited())
            {
                chain.push_back(entry);
            }
            // The engine's order goes from the base class to the class itself.
            std::reverse(chain.begin(), chain.end());
        }
        else if (!nav.empty())
        {
            chain = logical_path(nav);
        }
        if (reversed)
        {
            std::reverse(chain.begin(), chain.end());
        }
        std::vector<value> out;
        for (auto& entry : chain)
        {
            if (classes_only && !is_class(entry))
            {
                continue;
            }
            out.push_back(config_or_name(entry, names));
        }
        return std::make_shared<d_array>(out);
    }

    void collect_classes(const confignav& nav, int depth, bool inherited, bool all_tiers, bool names, std::vector<value>& out)
    {
        if (nav.empty() || nav->size() == 0)
        {
            return;
        }
        std::vector<confignav> children;
        if (inherited)
        {
            for (auto it = nav.recursive_begin(); it != nav.recursive_end(); ++it)
            {
                children.push_back(it.nav());
            }
        }
        else
        {
            for (auto it = nav.begin(); it != nav.end(); ++it)
            {
                children.push_back(it.nav());
            }
        }
        for (auto& child : children)
        {
            if (!is_class(child))
            {
                continue;
            }
            bool last_tier = depth <= 0;
            if (all_tiers || last_tier)
            {
                out.push_back(config_or_name(child, names));
            }
            if (!last_tier)
            {
                collect_classes(child, depth - 1, inherited, all_tiers, names, out);
            }
        }
    }

    /// configClasses [config, depth, includeInherited, allSubclasses, classNames] (2.22). Depth 0
    /// returns the direct subclasses; each extra level goes one tier deeper.
    value configclasses_array(runtime& runtime, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() < 4 || params->size() > 5 || !params->at(0).is<t_config>() || !params->at(1).is<t_scalar>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "configClasses", "expected [config, depth, includeInherited, allSubclasses, classNames]"));
            return std::make_shared<d_array>();
        }
        auto nav = params->at(0).data<d_config, config>().navigate(runtime.confighost());
        std::vector<value> out;
        collect_classes(nav,
            static_cast<int>(params->at(1).data<d_scalar, float>()),
            params->at(2).data_try<d_boolean, bool>(false),
            params->at(3).data_try<d_boolean, bool>(false),
            params->size() == 5 && params->at(4).data_try<d_boolean, bool>(false),
            out);
        return std::make_shared<d_array>(out);
    }

    /// getTextRaw config (2.02): the text as written, so a $STR_ key stays a key.
    value gettextraw_config(runtime& runtime, value::cref right)
    {
        auto nav = right.data<d_config, config>().navigate(runtime.confighost());
        if (nav.empty())
        {
            return std::string();
        }
        return nav->value.data_try<d_string, std::string>(std::string());
    }

    value mission_attribute(runtime& runtime, const std::string& attribute)
    {
        auto root = runtime.confighost().root() / mission_config_class;
        if (root.empty())
        {
            return {};
        }
        auto entry = root / attribute;
        // Classes have no value, so they return nil.
        if (entry.empty() || is_class(entry))
        {
            return {};
        }
        return entry->value;
    }
    value getmissionconfigvalue_string(runtime& runtime, value::cref right)
    {
        return mission_attribute(runtime, right.data<d_string, std::string>());
    }
    value getmissionconfigvalue_array(runtime& runtime, value::cref right)
    {
        auto params = right.data<d_array>();
        if (params->size() != 2 || !params->at(0).is<t_string>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "getMissionConfigValue", "expected [attribute, defaultValue]"));
            return {};
        }
        auto found = mission_attribute(runtime, params->at(0).data<d_string, std::string>());
        return found.empty() ? params->at(1) : found;
    }
    /// getMissionConfig attribute: the attribute's config entry.
    value getmissionconfig_string(runtime& runtime, value::cref right)
    {
        auto root = runtime.confighost().root() / mission_config_class;
        if (root.empty())
        {
            return config();
        }
        auto entry = root / right.data<d_string, std::string>();
        return entry.empty() ? value(config()) : value(*entry);
    }
}

void sqf::operators::ops_config_extra(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(nular("missionConfigFile", "The mission config (description.ext), read from class MissionConfigFile of the loaded config.",
        [](::sqf::runtime::runtime& runtime) -> value { return child_class_root(runtime, mission_config_class); }));
    runtime.register_sqfop(nular("campaignConfigFile", "The campaign config, read from class CampaignConfigFile of the loaded config.",
        [](::sqf::runtime::runtime& runtime) -> value { return child_class_root(runtime, campaign_config_class); }));
    runtime.register_sqfop(unary("getMissionConfigValue", t_string(), "Value of a mission attribute, or nil.", getmissionconfigvalue_string));
    runtime.register_sqfop(unary("getMissionConfigValue", t_array(), "getMissionConfigValue [attribute, defaultValue]", getmissionconfigvalue_array));
    runtime.register_sqfop(unary("getMissionConfig", t_string(), "Config entry of a mission attribute.", getmissionconfig_string));
    runtime.register_sqfop(unary("getTextRaw", t_config(), "Text of a config entry as written, without localisation.", gettextraw_config));
    runtime.register_sqfop(unary("configHierarchy", t_array(), "configHierarchy [config, classesOnly, includeBases, reversedOrder, configNames]", confighierarchy_array));
    runtime.register_sqfop(unary("configClasses", t_array(), "configClasses [config, depth, includeInherited, allSubclasses, classNames]", configclasses_array));
}
