#include "ops_world.h"
#include "world_events.h"
#include "object.h"
#include "d_object.h"
#include "d_config.h"
#include "ops_markers.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    constexpr float pi = 3.14159265358979323846f;
    // Cargo seats of a vehicle without a config class, so scripts can still load units.
    constexpr size_t unconfigured_cargo_seats = 32;

    using vec = std::array<float, 3>;

    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
    }

    std::string lowercase(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    vec to_vec(const sqf::runtime::vec3& v) { return { v.x, v.y, v.z }; }
    sqf::runtime::vec3 to_vec3(const vec& v) { return { v[0], v[1], v[2] }; }
    value vec_value(const vec& v) { return std::make_shared<d_array>(std::vector<value>{ v[0], v[1], v[2] }); }
    vec add(const vec& a, const vec& b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
    vec sub(const vec& a, const vec& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
    vec scale(const vec& a, float f) { return { a[0] * f, a[1] * f, a[2] * f }; }
    float dot(const vec& a, const vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    vec cross(const vec& a, const vec& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
    vec normalized(const vec& a)
    {
        auto length = std::sqrt(dot(a, a));
        return length == 0 ? a : scale(a, 1.0f / length);
    }

    std::shared_ptr<object> object_of(value::cref source)
    {
        auto handle = source.data_try<d_object>();
        if (!handle || handle->is_null())
        {
            return {};
        }
        return handle->value();
    }

    value object_value(const std::shared_ptr<object>& obj)
    {
        return obj ? value(std::make_shared<d_object>(obj)) : value(std::make_shared<d_object>());
    }

    /// A unit inside a vehicle is where the vehicle is.
    vec position_of(const std::shared_ptr<object>& obj)
    {
        if (auto vehicle = obj->world().vehicle.lock())
        {
            return to_vec(vehicle->position());
        }
        return to_vec(obj->position());
    }

    std::optional<vec> read_position(value::cref source)
    {
        if (auto obj = object_of(source))
        {
            return position_of(obj);
        }
        auto arr = source.data_try<d_array>();
        if (!arr || arr->size() < 2 || arr->size() > 3)
        {
            return {};
        }
        vec out{ 0, 0, 0 };
        for (size_t i = 0; i < arr->size(); i++)
        {
            if (!arr->at(i).is<t_scalar>())
            {
                return {};
            }
            out[i] = arr->at(i).data<d_scalar, float>();
        }
        return out;
    }

    std::shared_ptr<object> require_object(runtime& runtime, value::cref source)
    {
        auto obj = object_of(source);
        if (!obj)
        {
            runtime.__logmsg(err::ExpectedNonNullValueWeak(position(runtime)));
        }
        return obj;
    }

    // ---------------------------------------------------------------- positions

    /// Every position getter. The world is flat at height 0 without water, so ASL, ATL, AGL, AGLS and
    /// world positions are the same.
    value get_position(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        return obj ? vec_value(position_of(obj)) : vec_value({ 0, 0, 0 });
    }
    value set_position(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto target = read_position(right);
        if (!obj || !target)
        {
            if (obj)
            {
                runtime.__logmsg(err::ErrorMessage(position(runtime), "setPos", "expected a 2D or 3D position"));
            }
            return {};
        }
        obj->position(to_vec3(*target));
        return {};
    }
    /// ASLToAGL, AGLToASL, ATLToASL, ASLToATL: the identity on a flat world at height 0.
    value convert_position(runtime& runtime, value::cref right)
    {
        auto target = read_position(right);
        return target ? vec_value(*target) : value{};
    }
    /// object setVehiclePosition [position, markers, placement, special]: places the object at the
    /// position, or at a random listed marker.
    value setvehicleposition_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() < 1)
        {
            return false;
        }
        auto target = read_position(params->at(0));
        if (params->size() >= 2 && params->at(1).is<t_array>() && !params->at(1).data<d_array>()->empty())
        {
            auto markers = params->at(1).data<d_array>();
            auto name = markers->at(static_cast<size_t>(std::rand()) % markers->size()).data_try<d_string, std::string>(std::string());
            auto& storage = runtime.storage<sqf::operators::markers_storage>();
            if (storage.exists(name))
            {
                auto pos = storage.at(name).get_pos();
                target = vec{ pos[0], pos[1], pos[2] };
            }
        }
        if (!target)
        {
            return false;
        }
        obj->position(to_vec3(*target));
        return true;
    }

    // ---------------------------------------------------------------- orientation

    float azimuth_of(const vec& direction)
    {
        auto degrees = std::atan2(direction[0], direction[1]) * 180.0f / pi;
        return degrees < 0 ? degrees + 360.0f : degrees;
    }
    vec side_vector(const std::shared_ptr<object>& obj)
    {
        return normalized(cross(to_vec(obj->world().vector_dir), to_vec(obj->world().vector_up)));
    }
    value getdir_object(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        return obj ? azimuth_of(to_vec(obj->world().vector_dir)) : 0.0f;
    }
    value setdir_object_scalar(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto radians = right.data<d_scalar, float>() * pi / 180.0f;
        obj->world().vector_dir = { std::sin(radians), std::cos(radians), 0 };
        obj->world().vector_up = { 0, 0, 1 };
        return {};
    }
    /// a getDir b: azimuth from the first object or position to the second.
    value getdir_between(runtime& runtime, value::cref left, value::cref right)
    {
        auto from = read_position(left);
        auto to = read_position(right);
        if (!from || !to)
        {
            return 0.0f;
        }
        return azimuth_of(sub(*to, *from));
    }
    value vectordir_object(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        return obj ? vec_value(to_vec(obj->world().vector_dir)) : vec_value({ 0, 1, 0 });
    }
    value vectorup_object(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        return obj ? vec_value(to_vec(obj->world().vector_up)) : vec_value({ 0, 0, 1 });
    }
    value vectorside_object(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        return obj ? vec_value(side_vector(obj)) : vec_value({ 1, 0, 0 });
    }
    std::optional<vec> read_vector(runtime& runtime, value::cref source, const char* command)
    {
        auto parsed = read_position(source);
        if (!parsed || object_of(source))
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), command, "expected a 3D vector"));
            return {};
        }
        return parsed;
    }
    value setvectordir_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto direction = read_vector(runtime, right, "setVectorDir");
        if (obj && direction)
        {
            obj->world().vector_dir = to_vec3(normalized(*direction));
        }
        return {};
    }
    value setvectorup_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto up = read_vector(runtime, right, "setVectorUp");
        if (obj && up)
        {
            obj->world().vector_up = to_vec3(normalized(*up));
        }
        return {};
    }
    value setvectordirandup_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() != 2)
        {
            return {};
        }
        auto direction = read_vector(runtime, params->at(0), "setVectorDirAndUp");
        auto up = read_vector(runtime, params->at(1), "setVectorDirAndUp");
        if (direction && up)
        {
            obj->world().vector_dir = to_vec3(normalized(*direction));
            obj->world().vector_up = to_vec3(normalized(*up));
        }
        return {};
    }

    /// Model space: x to the right, y forward, z up.
    vec model_to_world_offset(const std::shared_ptr<object>& obj, const vec& offset)
    {
        auto side = side_vector(obj);
        auto forward = to_vec(obj->world().vector_dir);
        auto up = to_vec(obj->world().vector_up);
        return add(add(scale(side, offset[0]), scale(forward, offset[1])), scale(up, offset[2]));
    }
    vec world_to_model_offset(const std::shared_ptr<object>& obj, const vec& offset)
    {
        return { dot(offset, side_vector(obj)), dot(offset, to_vec(obj->world().vector_dir)), dot(offset, to_vec(obj->world().vector_up)) };
    }
    value modeltoworld_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto offset = read_vector(runtime, right, "modelToWorld");
        if (!obj || !offset)
        {
            return vec_value({ 0, 0, 0 });
        }
        return vec_value(add(position_of(obj), model_to_world_offset(obj, *offset)));
    }
    value worldtomodel_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto target = read_vector(runtime, right, "worldToModel");
        if (!obj || !target)
        {
            return vec_value({ 0, 0, 0 });
        }
        return vec_value(world_to_model_offset(obj, sub(*target, position_of(obj))));
    }
    value vectormodeltoworld_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto offset = read_vector(runtime, right, "vectorModelToWorld");
        return obj && offset ? vec_value(model_to_world_offset(obj, *offset)) : vec_value({ 0, 0, 0 });
    }
    value vectorworldtomodel_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto direction = read_vector(runtime, right, "vectorWorldToModel");
        return obj && direction ? vec_value(world_to_model_offset(obj, *direction)) : vec_value({ 0, 0, 0 });
    }
    /// object getRelPos [distance, direction]: the position at that distance and direction relative to
    /// where the object faces, at ground level.
    value getrelpos_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() != 2 || !params->at(0).is<t_scalar>() || !params->at(1).is<t_scalar>())
        {
            return vec_value({ 0, 0, 0 });
        }
        auto distance = params->at(0).data<d_scalar, float>();
        auto radians = (azimuth_of(to_vec(obj->world().vector_dir)) + params->at(1).data<d_scalar, float>()) * pi / 180.0f;
        auto origin = position_of(obj);
        return vec_value({ origin[0] + std::sin(radians) * distance, origin[1] + std::cos(radians) * distance, 0 });
    }
    /// object getRelDir position: direction to the position relative to where the object faces.
    value getreldir_object_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto target = read_position(right);
        if (!obj || !target)
        {
            return 0.0f;
        }
        auto relative = azimuth_of(sub(*target, position_of(obj))) - azimuth_of(to_vec(obj->world().vector_dir));
        return relative < 0 ? relative + 360.0f : relative;
    }

    // ---------------------------------------------------------------- stored properties

    enum class property_kind { number, boolean, text, anything };
    struct property_spec
    {
        const char* setter;
        const char* getter;
        const char* key;
        property_kind kind;
        float default_number;
        bool default_boolean;
        const char* default_text;
    };

    /// Setters whose value the matching getter returns. Defaults are the engine's values for a new unit
    /// or vehicle.
    constexpr property_spec property_specs[] = {
        { "setFatigue", "getFatigue", "fatigue", property_kind::number, 0, false, "" },
        { "setStamina", "getStamina", "stamina", property_kind::number, 60, false, "" },
        { "setFuel", "fuel", "fuel", property_kind::number, 1, false, "" },
        { "setCaptive", "captive", "captive", property_kind::boolean, 0, false, "" },
        { "allowDamage", "isDamageAllowed", "allowDamage", property_kind::boolean, 0, true, "" },
        { "hideObject", "isObjectHidden", "hidden", property_kind::boolean, 0, false, "" },
        { "hideObjectGlobal", "isObjectHidden", "hidden", property_kind::boolean, 0, false, "" },
        { "enableSimulation", "simulationEnabled", "simulation", property_kind::boolean, 0, true, "" },
        { "enableSimulationGlobal", "simulationEnabled", "simulation", property_kind::boolean, 0, true, "" },
        { "setUnitPos", "unitPos", "unitPos", property_kind::text, 0, false, "Auto" },
        { "setRank", "rank", "rank", property_kind::text, 0, false, "PRIVATE" },
        { "setFace", "face", "face", property_kind::text, 0, false, "" },
        { "setSpeaker", "speaker", "speaker", property_kind::text, 0, false, "" },
        { "setPitch", "pitch", "pitch", property_kind::number, 1, false, "" },
        { "setMass", "getMass", "mass", property_kind::number, 0, false, "" },
        { "setObjectScale", "getObjectScale", "objectScale", property_kind::number, 1, false, "" },
        { "setOxygenRemaining", "getOxygenRemaining", "oxygen", property_kind::number, 1, false, "" },
        { "setAnimSpeedCoef", "getAnimSpeedCoef", "animSpeedCoef", property_kind::number, 1, false, "" },
        { "setCustomAimCoef", "getCustomAimCoef", "customAimCoef", property_kind::number, 1, false, "" },
        { "engineOn", "isEngineOn", "engineOn", property_kind::boolean, 0, false, "" },
        { "setCombatMode", "combatMode", "combatMode", property_kind::text, 0, false, "YELLOW" },
        { "setBehaviour", "behaviour", "behaviour", property_kind::text, 0, false, "AWARE" },
        { "setSpeedMode", "speedMode", "speedMode", property_kind::text, 0, false, "NORMAL" },
        { "setFormation", "formation", "formation", property_kind::text, 0, false, "WEDGE" },
        { "setWaterLeakiness", "getWaterLeakiness", "waterLeakiness", property_kind::number, 1, false, "" },
        { "setWaterFillPercentage", "getWaterFillPercentage", "waterFill", property_kind::number, 0, false, "" },
        { "setCollisionLight", "isCollisionLightOn", "collisionLight", property_kind::boolean, 0, false, "" },
        { "setPilotLight", "isLightOn", "pilotLight", property_kind::boolean, 0, false, "" },
        { "setAutonomous", "isAutonomous", "autonomous", property_kind::boolean, 0, true, "" },
        { "setUnitRecoilCoefficient", "unitRecoilCoefficient", "recoilCoefficient", property_kind::number, 1, false, "" },
    };

    value property_default(const property_spec& spec)
    {
        switch (spec.kind)
        {
        case property_kind::number: return spec.default_number;
        case property_kind::boolean: return spec.default_boolean;
        case property_kind::text: return std::string(spec.default_text);
        default: return {};
        }
    }
    bool matches_kind(const property_spec& spec, value::cref source)
    {
        switch (spec.kind)
        {
        case property_kind::number: return source.is<t_scalar>();
        case property_kind::boolean: return source.is<t_boolean>();
        case property_kind::text: return source.is<t_string>();
        default: return true;
        }
    }

    template<size_t Index>
    value property_setter(runtime& runtime, value::cref left, value::cref right)
    {
        const auto& spec = property_specs[Index];
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        if (!matches_kind(spec, right))
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), spec.setter, "argument has the wrong type"));
            return {};
        }
        auto stored = right;
        if (spec.kind == property_kind::number && (std::string(spec.key) == "fatigue" || std::string(spec.key) == "fuel"))
        {
            stored = std::clamp(right.data<d_scalar, float>(), 0.0f, 1.0f);
        }
        obj->world().properties[spec.key] = stored;
        return {};
    }
    template<size_t Index>
    value property_getter(runtime& runtime, value::cref right)
    {
        const auto& spec = property_specs[Index];
        auto obj = object_of(right);
        if (!obj)
        {
            return property_default(spec);
        }
        auto found = obj->world().properties.find(spec.key);
        return found == obj->world().properties.end() ? property_default(spec) : found->second;
    }

    template<size_t... Indexes>
    void register_properties(runtime& runtime, std::index_sequence<Indexes...>)
    {
        using namespace sqf::runtime::sqfop;
        auto register_one = [&runtime](const property_spec& spec, sqfop_binary::callback setter, sqfop_unary::callback getter)
        {
            type argument_type = spec.kind == property_kind::number ? type(t_scalar()) : spec.kind == property_kind::boolean ? type(t_boolean()) : spec.kind == property_kind::text ? type(t_string()) : type(t_any());
            if (!runtime.sqfop_exists(sqfop_binary::key{ lowercase(spec.setter), t_object(), argument_type }))
            {
                runtime.register_sqfop(binary(4, spec.setter, t_object(), argument_type, "Stores the value for the matching getter.", setter));
            }
            if (!runtime.sqfop_exists(sqfop_unary::key{ lowercase(spec.getter), t_object() }))
            {
                runtime.register_sqfop(unary(spec.getter, t_object(), "Returns the value stored by the matching setter, or the default.", getter));
            }
        };
        (register_one(property_specs[Indexes], &property_setter<Indexes>, &property_getter<Indexes>), ...);
    }

    /// setUnconscious and lifeState: "DEAD" for a dead unit, "INCAPACITATED" while unconscious, else "HEALTHY".
    value setunconscious_object_boolean(runtime& runtime, value::cref left, value::cref right)
    {
        if (auto obj = require_object(runtime, left))
        {
            obj->world().properties["unconscious"] = right;
        }
        return {};
    }
    value lifestate_object(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        if (!obj || !obj->alive())
        {
            return std::string("DEAD");
        }
        auto found = obj->world().properties.find("unconscious");
        bool unconscious = found != obj->world().properties.end() && found->second.data_try<d_boolean, bool>(false);
        return std::string(unconscious ? "INCAPACITATED" : "HEALTHY");
    }

    // Per-key properties: enableAI / disableAI / checkAIFeature, setUnitTrait / getUnitTrait,
    // setHitPointDamage / getHitPointDamage, setObjectTexture / getObjectTextures.
    value keyed_property(const std::shared_ptr<object>& obj, const std::string& group, const std::string& key)
    {
        auto found = obj->world().properties.find(group + ":" + lowercase(key));
        return found == obj->world().properties.end() ? value{} : found->second;
    }
    void store_keyed_property(const std::shared_ptr<object>& obj, const std::string& group, const std::string& key, value stored)
    {
        obj->world().properties[group + ":" + lowercase(key)] = stored;
    }
    value set_ai_feature(runtime& runtime, value::cref left, value::cref right, bool enabled)
    {
        auto obj = require_object(runtime, left);
        if (obj)
        {
            store_keyed_property(obj, "ai", right.data<d_string, std::string>(), enabled);
        }
        return {};
    }
    value checkaifeature_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return true;
        }
        auto stored = keyed_property(obj, "ai", right.data<d_string, std::string>());
        return stored.empty() ? true : stored.data<d_boolean, bool>();
    }
    value setunittrait_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() < 2 || !params->at(0).is<t_string>())
        {
            return {};
        }
        store_keyed_property(obj, "trait", params->at(0).data<d_string, std::string>(), params->at(1));
        return {};
    }
    value getunittrait_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto name = lowercase(right.data<d_string, std::string>());
        auto stored = keyed_property(obj, "trait", name);
        if (!stored.empty())
        {
            return stored;
        }
        // Engine defaults for the standard traits.
        if (name == "medic" || name == "engineer" || name == "explosivespecialist" || name == "uavhacker") { return false; }
        if (name == "camouflagecoef" || name == "audiblecoef" || name == "loadcoef") { return 1.0f; }
        return {};
    }
    value sethitpointdamage_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() < 2 || !params->at(0).is<t_string>() || !params->at(1).is<t_scalar>())
        {
            return {};
        }
        store_keyed_property(obj, "hitpoint", params->at(0).data<d_string, std::string>(), std::clamp(params->at(1).data<d_scalar, float>(), 0.0f, 1.0f));
        return {};
    }
    value gethitpointdamage_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return 0.0f;
        }
        auto stored = keyed_property(obj, "hitpoint", right.data<d_string, std::string>());
        return stored.empty() ? value(0.0f) : stored;
    }
    value setobjecttexture_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() != 2 || !params->at(0).is<t_scalar>() || !params->at(1).is<t_string>())
        {
            return {};
        }
        auto index = static_cast<size_t>(std::max(0.0f, params->at(0).data<d_scalar, float>()));
        auto textures = keyed_property(obj, "textures", "all");
        std::vector<value> list = textures.empty() ? std::vector<value>{} : textures.data<d_array>()->value();
        if (list.size() <= index)
        {
            list.resize(index + 1, value(std::string()));
        }
        list[index] = params->at(1);
        store_keyed_property(obj, "textures", "all", value(std::make_shared<d_array>(list)));
        return {};
    }
    value getobjecttextures_object(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        if (!obj)
        {
            return std::make_shared<d_array>();
        }
        auto textures = keyed_property(obj, "textures", "all");
        return textures.empty() ? value(std::make_shared<d_array>()) : value(textures.data<d_array>()->copy_deep());
    }

    // ---------------------------------------------------------------- damage

    value setdamage_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() < 1 || !params->at(0).is<t_scalar>())
        {
            return {};
        }
        bool use_effects = params->size() < 2 || params->at(1).data_try<d_boolean, bool>(true);
        auto killer = params->size() >= 3 ? params->at(2) : object_value({});
        auto instigator = params->size() >= 4 ? params->at(3) : object_value({});
        sqf::operators::apply_scripted_damage(runtime, obj, params->at(0).data<d_scalar, float>(), killer, instigator, use_effects);
        return {};
    }

    // ---------------------------------------------------------------- crew

    bool config_flag(runtime& runtime, const std::shared_ptr<object>& vehicle, const char* name, bool fallback)
    {
        auto nav = vehicle->config().navigate(runtime.confighost());
        if (nav.empty())
        {
            return fallback;
        }
        auto entry = nav / name;
        if (entry.empty())
        {
            return fallback;
        }
        return entry->value.data_try<d_scalar, float>(fallback ? 1.0f : 0.0f) != 0;
    }
    size_t cargo_capacity(runtime& runtime, const std::shared_ptr<object>& vehicle)
    {
        auto nav = vehicle->config().navigate(runtime.confighost());
        if (nav.empty())
        {
            return unconfigured_cargo_seats;
        }
        auto entry = nav / "transportSoldier";
        return entry.empty() ? 0 : static_cast<size_t>(entry->value.data_try<d_scalar, float>(0));
    }
    std::shared_ptr<object> unit_in_role(const std::shared_ptr<object>& vehicle, const std::string& role)
    {
        for (auto& seat : vehicle->world().crew)
        {
            if (seat.role == role)
            {
                return seat.unit.lock();
            }
        }
        return {};
    }
    void drop_missing_crew(const std::shared_ptr<object>& vehicle)
    {
        auto& crew = vehicle->world().crew;
        crew.erase(std::remove_if(crew.begin(), crew.end(), [](const object::world_state::crew_seat& seat) { return seat.unit.expired(); }), crew.end());
    }
    value turret_value(const std::vector<int>& path)
    {
        std::vector<value> out;
        for (auto index : path) { out.push_back(static_cast<float>(index)); }
        return std::make_shared<d_array>(out);
    }

    /// Removes the unit from its vehicle and fires GetOut and GetOutMan.
    void leave_vehicle(runtime& runtime, const std::shared_ptr<object>& unit)
    {
        auto vehicle = unit->world().vehicle.lock();
        unit->world().vehicle.reset();
        if (!vehicle)
        {
            return;
        }
        std::string role;
        std::vector<int> turret;
        auto& crew = vehicle->world().crew;
        for (auto it = crew.begin(); it != crew.end(); ++it)
        {
            if (it->unit.lock() == unit)
            {
                role = it->role;
                turret = it->turret_path;
                crew.erase(it);
                break;
            }
        }
        unit->position(vehicle->position());
        sqf::operators::fire_object_event(runtime, vehicle, "GetOut", { object_value(vehicle), role, object_value(unit), turret_value(turret) });
        sqf::operators::fire_object_event(runtime, unit, "GetOutMan", { object_value(unit), role, object_value(vehicle), turret_value(turret) });
    }

    /// Puts the unit in the seat if it is free and fires GetIn and GetInMan. Returns false if it is taken.
    bool enter_vehicle(runtime& runtime, const std::shared_ptr<object>& unit, const std::shared_ptr<object>& vehicle, const std::string& role, int cargo_index, std::vector<int> turret)
    {
        if (unit == vehicle || unit->is_vehicle())
        {
            return false;
        }
        drop_missing_crew(vehicle);
        if (role == "cargo")
        {
            size_t used = std::count_if(vehicle->world().crew.begin(), vehicle->world().crew.end(), [](const object::world_state::crew_seat& s) { return s.role == "cargo"; });
            if (used >= cargo_capacity(runtime, vehicle))
            {
                return false;
            }
            if (cargo_index < 0)
            {
                cargo_index = static_cast<int>(used);
            }
        }
        else if (unit_in_role(vehicle, role))
        {
            return false;
        }
        else if (!config_flag(runtime, vehicle, role == "driver" ? "hasDriver" : role == "gunner" ? "hasGunner" : "hasCommander", true))
        {
            return false;
        }
        if (unit->world().vehicle.lock())
        {
            leave_vehicle(runtime, unit);
        }
        if (role == "gunner" && turret.empty()) { turret = { 0 }; }
        if (role == "commander" && turret.empty()) { turret = { 0, 0 }; }
        vehicle->world().crew.push_back({ unit, role, role == "cargo" ? cargo_index : -1, turret });
        unit->world().vehicle = vehicle;
        sqf::operators::fire_object_event(runtime, vehicle, "GetIn", { object_value(vehicle), role, object_value(unit), turret_value(turret) });
        sqf::operators::fire_object_event(runtime, unit, "GetInMan", { object_value(unit), role, object_value(vehicle), turret_value(turret) });
        return true;
    }

    template<int RoleIndex>
    value movein_object_object(runtime& runtime, value::cref left, value::cref right)
    {
        static const char* roles[] = { "driver", "gunner", "commander", "cargo" };
        auto unit = require_object(runtime, left);
        auto vehicle = require_object(runtime, right);
        if (unit && vehicle)
        {
            enter_vehicle(runtime, unit, vehicle, roles[RoleIndex], -1, {});
        }
        return {};
    }
    /// unit moveInCargo [vehicle, cargoIndex, canReassign]
    value moveincargo_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto unit = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!unit || params->size() < 1)
        {
            return {};
        }
        auto vehicle = require_object(runtime, params->at(0));
        if (vehicle)
        {
            auto index = params->size() >= 2 ? static_cast<int>(params->at(1).data_try<d_scalar, float>(-1)) : -1;
            enter_vehicle(runtime, unit, vehicle, "cargo", index, {});
        }
        return {};
    }
    /// unit moveInTurret [vehicle, turretPath]
    value moveinturret_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto unit = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!unit || params->size() != 2 || !params->at(1).is<t_array>())
        {
            return {};
        }
        auto vehicle = require_object(runtime, params->at(0));
        if (!vehicle)
        {
            return {};
        }
        std::vector<int> path;
        for (auto& index : params->at(1).data<d_array>()->value())
        {
            path.push_back(static_cast<int>(index.data_try<d_scalar, float>(0)));
        }
        auto role = path == std::vector<int>{ 0 } ? "gunner" : path == std::vector<int>{ 0, 0 } ? "commander" : "turret";
        for (auto& seat : vehicle->world().crew)
        {
            if (seat.turret_path == path && !seat.unit.expired())
            {
                return {};
            }
        }
        enter_vehicle(runtime, unit, vehicle, role, -1, path);
        return {};
    }
    bool move_in_any(runtime& runtime, const std::shared_ptr<object>& unit, const std::shared_ptr<object>& vehicle, const std::vector<std::string>& roles)
    {
        for (auto& role : roles)
        {
            if (enter_vehicle(runtime, unit, vehicle, role, -1, {}))
            {
                return true;
            }
        }
        return false;
    }
    value moveinany_object_object(runtime& runtime, value::cref left, value::cref right)
    {
        auto unit = require_object(runtime, left);
        auto vehicle = require_object(runtime, right);
        return unit && vehicle && move_in_any(runtime, unit, vehicle, { "driver", "gunner", "commander", "cargo" });
    }
    /// unit moveInAny [vehicle, positions, ignoreLocked] (2.22)
    value moveinany_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto unit = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!unit || params->size() < 1)
        {
            return false;
        }
        auto vehicle = require_object(runtime, params->at(0));
        std::vector<std::string> roles = { "driver", "gunner", "commander", "cargo" };
        if (params->size() >= 2 && params->at(1).is<t_array>())
        {
            roles.clear();
            for (auto& role : params->at(1).data<d_array>()->value())
            {
                roles.push_back(lowercase(role.data_try<d_string, std::string>(std::string())));
            }
        }
        return vehicle && move_in_any(runtime, unit, vehicle, roles);
    }
    value moveout_object(runtime& runtime, value::cref right)
    {
        if (auto unit = require_object(runtime, right))
        {
            leave_vehicle(runtime, unit);
        }
        return {};
    }
    value moveout_object_object(runtime& runtime, value::cref left, value::cref right)
    {
        auto unit = require_object(runtime, left);
        auto vehicle = object_of(right);
        if (unit && vehicle && unit->world().vehicle.lock() == vehicle)
        {
            leave_vehicle(runtime, unit);
        }
        return {};
    }

    template<int RoleIndex>
    value role_getter(runtime& runtime, value::cref right)
    {
        static const char* roles[] = { "driver", "gunner", "commander" };
        auto obj = object_of(right);
        if (!obj)
        {
            return object_value({});
        }
        // A unit on foot is its own driver, gunner and commander, as in the engine.
        if (!obj->is_vehicle())
        {
            return object_value(obj);
        }
        return object_value(unit_in_role(obj, roles[RoleIndex]));
    }
    value crew_object(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        std::vector<value> out;
        if (obj)
        {
            if (!obj->is_vehicle())
            {
                out.push_back(object_value(obj));
            }
            for (auto& seat : obj->world().crew)
            {
                if (auto unit = seat.unit.lock())
                {
                    out.push_back(object_value(unit));
                }
            }
        }
        return std::make_shared<d_array>(out);
    }
    value vehicle_object(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        if (!obj)
        {
            return object_value({});
        }
        auto vehicle = obj->world().vehicle.lock();
        return object_value(vehicle ? vehicle : obj);
    }
    value objectparent_object(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        return object_value(obj ? obj->world().vehicle.lock() : std::shared_ptr<object>{});
    }
    value in_object_object(runtime& runtime, value::cref left, value::cref right)
    {
        auto unit = object_of(left);
        auto vehicle = object_of(right);
        return unit && vehicle && unit->world().vehicle.lock() == vehicle;
    }
    /// vehicle fullCrew: [[unit, role, cargoIndex, turretPath, isPersonTurret], ...]
    value fullcrew_object(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        std::vector<value> out;
        if (obj)
        {
            for (auto& seat : obj->world().crew)
            {
                if (auto unit = seat.unit.lock())
                {
                    out.push_back(value(std::make_shared<d_array>(std::vector<value>{
                        object_value(unit), seat.role, static_cast<float>(seat.cargo_index), turret_value(seat.turret_path), false })));
                }
            }
        }
        return std::make_shared<d_array>(out);
    }
    /// vehicle emptyPositions role: free seats of that role.
    value emptypositions_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto vehicle = object_of(left);
        if (!vehicle || !vehicle->is_vehicle())
        {
            return 0.0f;
        }
        drop_missing_crew(vehicle);
        auto role = lowercase(right.data<d_string, std::string>());
        if (role == "cargo")
        {
            size_t used = std::count_if(vehicle->world().crew.begin(), vehicle->world().crew.end(), [](const object::world_state::crew_seat& s) { return s.role == "cargo"; });
            return static_cast<float>(cargo_capacity(runtime, vehicle) - std::min(used, cargo_capacity(runtime, vehicle)));
        }
        if (role != "driver" && role != "gunner" && role != "commander")
        {
            return 0.0f;
        }
        bool exists = config_flag(runtime, vehicle, role == "driver" ? "hasDriver" : role == "gunner" ? "hasGunner" : "hasCommander", true);
        return exists && !unit_in_role(vehicle, role) ? 1.0f : 0.0f;
    }

    // ---------------------------------------------------------------- queries

    std::vector<std::shared_ptr<object>> all_objects(runtime& runtime)
    {
        std::vector<std::shared_ptr<object>> out;
        for (auto& obj : runtime.storage<object::object_storage>())
        {
            if (obj)
            {
                out.push_back(obj);
            }
        }
        return out;
    }

    /// True if the object's class is the type or inherits from it, through CfgVehicles.
    bool is_kind_of(runtime& runtime, const std::shared_ptr<object>& obj, const std::string& type)
    {
        auto wanted = lowercase(type);
        if (wanted.empty())
        {
            return true;
        }
        auto nav = obj->config().navigate(runtime.confighost());
        for (auto entry = nav; !entry.empty(); entry = entry.parent_inherited())
        {
            if (lowercase(std::string(entry->name)) == wanted)
            {
                return true;
            }
        }
        return false;
    }
    bool matches_types(runtime& runtime, const std::shared_ptr<object>& obj, const std::vector<value>& types, bool exact)
    {
        if (types.empty())
        {
            return true;
        }
        for (auto& type : types)
        {
            auto name = type.data_try<d_string, std::string>(std::string());
            if (exact ? lowercase(std::string(obj->config().name())) == lowercase(name) : is_kind_of(runtime, obj, name))
            {
                return true;
            }
        }
        return false;
    }
    std::vector<value> type_list(value::cref source)
    {
        if (source.is<t_string>())
        {
            return { source };
        }
        if (source.is<t_array>())
        {
            return source.data<d_array>()->value();
        }
        return {};
    }

    /// nearEntities [types, radius] around a position or object. Units in vehicles are not listed.
    std::vector<value> near_entities(runtime& runtime, const vec& center, float radius, const std::vector<value>& types, bool exact, bool alive_only, bool include_crew)
    {
        std::vector<std::pair<float, std::shared_ptr<object>>> found;
        for (auto& obj : all_objects(runtime))
        {
            if (!include_crew && obj->world().vehicle.lock())
            {
                continue;
            }
            if (alive_only && !obj->alive())
            {
                continue;
            }
            auto offset = sub(position_of(obj), center);
            auto distance = std::sqrt(dot(offset, offset));
            if (distance <= radius && matches_types(runtime, obj, types, exact))
            {
                found.push_back({ distance, obj });
            }
        }
        std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first < b.first; });
        std::vector<value> out;
        for (auto& entry : found) { out.push_back(object_value(entry.second)); }
        return out;
    }
    value nearentities_any_scalar(runtime& runtime, value::cref left, value::cref right)
    {
        auto center = read_position(left);
        if (!center)
        {
            return std::make_shared<d_array>();
        }
        return std::make_shared<d_array>(near_entities(runtime, *center, right.data<d_scalar, float>(), {}, false, true, false));
    }
    value nearentities_any_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto params = right.data<d_array>();
        auto center = read_position(left);
        // The 2.18 area form: area nearEntities [types, matchExactType, aliveOnly, includeCrew].
        if (!center || (params->size() >= 1 && params->size() <= 4 && (params->size() < 2 || params->at(1).is<t_boolean>()) && left.is<t_array>() && left.data<d_array>()->size() >= 3 && left.data<d_array>()->at(0).is<t_array>()))
        {
            auto area = left.data_try<d_array>();
            if (!area || area->size() < 3)
            {
                return std::make_shared<d_array>();
            }
            auto area_center = read_position(area->at(0));
            auto a = area->at(1).data_try<d_scalar, float>(0);
            auto b = area->at(2).data_try<d_scalar, float>(0);
            if (!area_center)
            {
                return std::make_shared<d_array>();
            }
            auto candidates = near_entities(runtime, *area_center, std::sqrt(a * a + b * b),
                params->size() >= 1 ? type_list(params->at(0)) : std::vector<value>{},
                params->size() >= 2 && params->at(1).data_try<d_boolean, bool>(false),
                params->size() < 3 || params->at(2).data_try<d_boolean, bool>(true),
                params->size() >= 4 && params->at(3).data_try<d_boolean, bool>(false));
            return std::make_shared<d_array>(candidates);
        }
        if (params->size() != 2 || !params->at(1).is<t_scalar>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "nearEntities", "expected [types, radius]"));
            return std::make_shared<d_array>();
        }
        return std::make_shared<d_array>(near_entities(runtime, *center, params->at(1).data<d_scalar, float>(), type_list(params->at(0)), false, true, false));
    }
    /// position nearestObject type: the closest object of the type, or objNull.
    value nearestobject_any_string(runtime& runtime, value::cref left, value::cref right)
    {
        auto center = read_position(left);
        if (!center)
        {
            return object_value({});
        }
        auto found = near_entities(runtime, *center, 50.0f, { right }, false, false, true);
        return found.empty() ? object_value({}) : found.front();
    }
    /// entities type / entities [[include], [exclude], includeCrew, excludeDead]
    value entities_any(runtime& runtime, value::cref right)
    {
        std::vector<value> include = right.is<t_string>() ? std::vector<value>{ right } : std::vector<value>{};
        std::vector<value> exclude;
        bool include_crew = false;
        bool exclude_dead = false;
        if (right.is<t_array>())
        {
            auto params = right.data<d_array>();
            if (params->size() >= 1) { include = type_list(params->at(0)); }
            if (params->size() >= 2) { exclude = type_list(params->at(1)); }
            if (params->size() >= 3) { include_crew = params->at(2).data_try<d_boolean, bool>(false); }
            if (params->size() >= 4) { exclude_dead = params->at(3).data_try<d_boolean, bool>(false); }
        }
        std::vector<value> out;
        for (auto& obj : all_objects(runtime))
        {
            if (!include_crew && obj->world().vehicle.lock()) { continue; }
            if (exclude_dead && !obj->alive()) { continue; }
            if (!matches_types(runtime, obj, include, false)) { continue; }
            if (!exclude.empty() && matches_types(runtime, obj, exclude, false)) { continue; }
            out.push_back(object_value(obj));
        }
        return std::make_shared<d_array>(out);
    }
    template<int Filter>
    value object_list(runtime& runtime)
    {
        std::vector<value> out;
        for (auto& obj : all_objects(runtime))
        {
            bool keep = false;
            switch (Filter)
            {
            case 0: keep = obj->is_vehicle(); break;                      // vehicles
            case 1: keep = !obj->alive(); break;                           // allDead
            case 2: keep = !obj->alive() && !obj->is_vehicle(); break;     // allDeadMen
            }
            if (keep)
            {
                out.push_back(object_value(obj));
            }
        }
        return std::make_shared<d_array>(out);
    }
    /// allPlayers, playableUnits and switchableUnits: SQF-VM has one player.
    value player_list(runtime& runtime)
    {
        auto player = runtime.storage<object::object_storage>().player();
        std::vector<value> out;
        if (player)
        {
            out.push_back(object_value(player));
        }
        return std::make_shared<d_array>(out);
    }

    struct area
    {
        vec center;
        float a;
        float b;
        float angle;
        bool rectangle;
        float c;
    };
    /// An area is [center, a, b, angle, isRectangle, c], a marker name, or an object or location
    /// handle; objects are treated as a point with a 0 radius.
    std::optional<area> read_area(runtime& runtime, value::cref source)
    {
        if (source.is<t_string>())
        {
            auto& storage = runtime.storage<sqf::operators::markers_storage>();
            auto name = source.data<d_string, std::string>();
            if (!storage.exists(name))
            {
                return {};
            }
            auto& marker = storage.at(name);
            auto pos = marker.get_pos();
            auto size = marker.get_size();
            return area{ { pos[0], pos[1], pos[2] }, size[0], size[1], marker.get_direction(),
                marker.get_shape() == sqf::operators::markers_storage::marker::shape::Rectangle, -1 };
        }
        auto arr = source.data_try<d_array>();
        if (!arr || arr->size() < 3)
        {
            return {};
        }
        auto center = read_position(arr->at(0));
        if (!center)
        {
            return {};
        }
        return area{ *center,
            arr->at(1).data_try<d_scalar, float>(0),
            arr->at(2).data_try<d_scalar, float>(0),
            arr->size() >= 4 ? arr->at(3).data_try<d_scalar, float>(0) : 0.0f,
            arr->size() >= 5 && arr->at(4).data_try<d_boolean, bool>(false),
            arr->size() >= 6 ? arr->at(5).data_try<d_scalar, float>(-1) : -1.0f };
    }
    bool in_area(const area& zone, const vec& point)
    {
        auto offset = sub(point, zone.center);
        if (zone.c >= 0 && std::abs(offset[2]) > zone.c)
        {
            return false;
        }
        // Rotate into the area's frame; the angle is clockwise from north.
        auto radians = zone.angle * pi / 180.0f;
        auto x = offset[0] * std::cos(radians) - offset[1] * std::sin(radians);
        auto y = offset[0] * std::sin(radians) + offset[1] * std::cos(radians);
        if (zone.a <= 0 || zone.b <= 0)
        {
            return false;
        }
        if (zone.rectangle)
        {
            return std::abs(x) <= zone.a && std::abs(y) <= zone.b;
        }
        return (x * x) / (zone.a * zone.a) + (y * y) / (zone.b * zone.b) <= 1.0f;
    }
    value inarea_any_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto point = read_position(left);
        auto zone = read_area(runtime, right);
        return point && zone && in_area(*zone, *point);
    }
    template<bool Indexes>
    value inareaarray_array_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto zone = read_area(runtime, right);
        auto items = left.data<d_array>();
        std::vector<value> out;
        if (!zone)
        {
            return std::make_shared<d_array>(out);
        }
        for (size_t i = 0; i < items->size(); i++)
        {
            auto point = read_position(items->at(i));
            if (point && in_area(*zone, *point))
            {
                out.push_back(Indexes ? value(static_cast<float>(i)) : items->at(i));
            }
        }
        return std::make_shared<d_array>(out);
    }
}

void sqf::operators::apply_scripted_damage(::sqf::runtime::runtime& runtime, const std::shared_ptr<::sqf::types::object>& target, float damage,
    ::sqf::runtime::value killer, ::sqf::runtime::value instigator, bool use_effects)
{
    bool was_alive = target->alive();
    target->damage(damage);
    if (was_alive && !target->alive())
    {
        auto victim = object_value(target);
        std::vector<value> params{ victim, killer, instigator, use_effects };
        fire_object_and_mission_event(runtime, target, "Killed", params, "EntityKilled", params);
    }
}

void sqf::operators::ops_world(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;

    for (auto name : { "getPosASL", "getPosATL", "getPosWorld", "getPosASLW", "getPosASLVisual", "getPosATLVisual", "getPosWorldVisual", "getPosVisual", "visiblePosition", "visiblePositionASL" })
    {
        runtime.register_sqfop(unary(name, t_object(), "Position of the object. The world is flat at height 0, so every position form is the same.", get_position));
    }
    for (auto name : { "setPosASL", "setPosATL", "setPosWorld", "setPosASLW", "setPosASL2" })
    {
        runtime.register_sqfop(binary(4, name, t_object(), t_array(), "Moves the object. The world is flat at height 0, so every position form is the same.", set_position));
    }
    for (auto name : { "ASLToAGL", "AGLToASL", "ATLToASL", "ASLToATL" })
    {
        runtime.register_sqfop(unary(name, t_array(), "Converts between position forms; on a flat world at height 0 the position is unchanged.", convert_position));
    }
    runtime.register_sqfop(binary(4, "setVehiclePosition", t_object(), t_array(), "Places the object at the position, or at one of the listed markers.", setvehicleposition_object_array));

    runtime.register_sqfop(unary("getDir", t_object(), "Direction the object faces, in degrees.", getdir_object));
    runtime.register_sqfop(unary("getDirVisual", t_object(), "Direction the object faces, in degrees.", getdir_object));
    runtime.register_sqfop(binary(4, "setDir", t_object(), t_scalar(), "Turns the object to face the direction and levels it.", setdir_object_scalar));
    for (auto pair : { std::pair<type, type>{ t_object(), t_object() }, std::pair<type, type>{ t_object(), t_array() }, std::pair<type, type>{ t_array(), t_object() }, std::pair<type, type>{ t_array(), t_array() } })
    {
        runtime.register_sqfop(binary(4, "getDir", pair.first, pair.second, "Direction from the first position or object to the second.", getdir_between));
        runtime.register_sqfop(binary(4, "getDirVisual", pair.first, pair.second, "Direction from the first position or object to the second.", getdir_between));
    }
    for (auto name : { "vectorDir", "vectorDirVisual" }) { runtime.register_sqfop(unary(name, t_object(), "Direction vector of the object.", vectordir_object)); }
    for (auto name : { "vectorUp", "vectorUpVisual" }) { runtime.register_sqfop(unary(name, t_object(), "Up vector of the object.", vectorup_object)); }
    for (auto name : { "vectorSide", "vectorSideVisual" }) { runtime.register_sqfop(unary(name, t_object(), "Side vector of the object, pointing right.", vectorside_object)); }
    runtime.register_sqfop(binary(4, "setVectorDir", t_object(), t_array(), "Sets the direction vector.", setvectordir_object_array));
    runtime.register_sqfop(binary(4, "setVectorUp", t_object(), t_array(), "Sets the up vector.", setvectorup_object_array));
    runtime.register_sqfop(binary(4, "setVectorDirAndUp", t_object(), t_array(), "setVectorDirAndUp [direction, up]", setvectordirandup_object_array));
    for (auto name : { "modelToWorld", "modelToWorldVisual", "modelToWorldWorld", "modelToWorldVisualWorld" })
    {
        runtime.register_sqfop(binary(4, name, t_object(), t_array(), "Converts a model-space offset to a world position.", modeltoworld_object_array));
    }
    for (auto name : { "worldToModel", "worldToModelVisual" })
    {
        runtime.register_sqfop(binary(4, name, t_object(), t_array(), "Converts a world position to a model-space offset.", worldtomodel_object_array));
    }
    for (auto name : { "vectorModelToWorld", "vectorModelToWorldVisual" })
    {
        runtime.register_sqfop(binary(4, name, t_object(), t_array(), "Rotates a model-space vector into world space.", vectormodeltoworld_object_array));
    }
    for (auto name : { "vectorWorldToModel", "vectorWorldToModelVisual" })
    {
        runtime.register_sqfop(binary(4, name, t_object(), t_array(), "Rotates a world-space vector into model space.", vectorworldtomodel_object_array));
    }
    runtime.register_sqfop(binary(4, "getRelPos", t_object(), t_array(), "object getRelPos [distance, direction]", getrelpos_object_array));
    runtime.register_sqfop(binary(4, "getRelDir", t_object(), t_any(), "Direction to the position relative to where the object faces.", getreldir_object_any));

    register_properties(runtime, std::make_index_sequence<std::size(property_specs)>());
    runtime.register_sqfop(binary(4, "setUnconscious", t_object(), t_boolean(), "Makes the unit unconscious or wakes it up.", setunconscious_object_boolean));
    runtime.register_sqfop(unary("lifeState", t_object(), "\"HEALTHY\", \"INCAPACITATED\" or \"DEAD\".", lifestate_object));
    runtime.register_sqfop(binary(4, "enableAI", t_object(), t_string(), "Enables an AI feature of the unit.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return set_ai_feature(runtime, left, right, true); }));
    runtime.register_sqfop(binary(4, "disableAI", t_object(), t_string(), "Disables an AI feature of the unit.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return set_ai_feature(runtime, left, right, false); }));
    runtime.register_sqfop(binary(4, "checkAIFeature", t_object(), t_string(), "True if the AI feature is enabled.", checkaifeature_object_string));
    runtime.register_sqfop(binary(4, "setUnitTrait", t_object(), t_array(), "setUnitTrait [name, value, isCustom]", setunittrait_object_array));
    runtime.register_sqfop(binary(4, "getUnitTrait", t_object(), t_string(), "Value of the unit trait.", getunittrait_object_string));
    runtime.register_sqfop(binary(4, "setHitPointDamage", t_object(), t_array(), "setHitPointDamage [hitPoint, damage, useEffects, killer, instigator, breakRotor]", sethitpointdamage_object_array));
    runtime.register_sqfop(binary(4, "getHitPointDamage", t_object(), t_string(), "Damage of the hit point.", gethitpointdamage_object_string));
    runtime.register_sqfop(binary(4, "setObjectTexture", t_object(), t_array(), "setObjectTexture [selectionIndex, texture]", setobjecttexture_object_array));
    runtime.register_sqfop(binary(4, "setObjectTextureGlobal", t_object(), t_array(), "setObjectTextureGlobal [selectionIndex, texture]", setobjecttexture_object_array));
    runtime.register_sqfop(unary("getObjectTextures", t_object(), "Textures set with setObjectTexture.", getobjecttextures_object));

    runtime.register_sqfop(binary(4, "setDamage", t_object(), t_array(), "setDamage [damage, useEffects, killer, instigator]: fires Killed and EntityKilled when the object dies.", setdamage_object_array));

    runtime.register_sqfop(binary(4, "moveInDriver", t_object(), t_object(), "Moves the unit into the driver seat.", movein_object_object<0>));
    runtime.register_sqfop(binary(4, "moveInGunner", t_object(), t_object(), "Moves the unit into the gunner seat.", movein_object_object<1>));
    runtime.register_sqfop(binary(4, "moveInCommander", t_object(), t_object(), "Moves the unit into the commander seat.", movein_object_object<2>));
    runtime.register_sqfop(binary(4, "moveInCargo", t_object(), t_object(), "Moves the unit into a free cargo seat.", movein_object_object<3>));
    runtime.register_sqfop(binary(4, "moveInCargo", t_object(), t_array(), "moveInCargo [vehicle, cargoIndex, canReassign]", moveincargo_object_array));
    runtime.register_sqfop(binary(4, "moveInTurret", t_object(), t_array(), "moveInTurret [vehicle, turretPath]", moveinturret_object_array));
    runtime.register_sqfop(binary(4, "moveInAny", t_object(), t_object(), "Moves the unit into the first free seat; returns true on success.", moveinany_object_object));
    runtime.register_sqfop(binary(4, "moveInAny", t_object(), t_array(), "moveInAny [vehicle, positions, ignoreLocked]", moveinany_object_array));
    runtime.register_sqfop(unary("moveOut", t_object(), "Moves the unit out of its vehicle.", moveout_object));
    runtime.register_sqfop(binary(4, "moveOut", t_object(), t_object(), "Moves the unit out of the vehicle.", moveout_object_object));
    runtime.register_sqfop(unary("driver", t_object(), "Driver of the vehicle; a unit on foot is its own driver.", role_getter<0>));
    runtime.register_sqfop(unary("gunner", t_object(), "Gunner of the vehicle; a unit on foot is its own gunner.", role_getter<1>));
    runtime.register_sqfop(unary("commander", t_object(), "Commander of the vehicle; a unit on foot is its own commander.", role_getter<2>));
    runtime.register_sqfop(unary("crew", t_object(), "Units in the vehicle; a unit on foot is its own crew.", crew_object));
    runtime.register_sqfop(unary("vehicle", t_object(), "Vehicle the unit is in, or the unit itself.", vehicle_object));
    runtime.register_sqfop(unary("objectParent", t_object(), "Vehicle the unit is in, or objNull.", objectparent_object));
    runtime.register_sqfop(binary(4, "in", t_object(), t_object(), "True if the unit is in the vehicle.", in_object_object));
    runtime.register_sqfop(unary("fullCrew", t_object(), "[[unit, role, cargoIndex, turretPath, isPersonTurret], ...]", fullcrew_object));
    runtime.register_sqfop(binary(4, "emptyPositions", t_object(), t_string(), "Number of free seats of the role.", emptypositions_object_string));

    runtime.register_sqfop(binary(4, "nearEntities", t_any(), t_scalar(), "Living entities within the radius, nearest first.", nearentities_any_scalar));
    runtime.register_sqfop(binary(4, "nearEntities", t_any(), t_array(), "position nearEntities [types, radius], or the 2.18 area form.", nearentities_any_array));
    runtime.register_sqfop(binary(4, "nearestObject", t_any(), t_string(), "Nearest object of the type within 50 m, or objNull.", nearestobject_any_string));
    runtime.register_sqfop(unary("entities", t_any(), "entities type, or entities [[include], [exclude], includeCrew, excludeDead]", entities_any));
    runtime.register_sqfop(nular("vehicles", "All vehicles.", object_list<0>));
    runtime.register_sqfop(nular("allDead", "All dead objects.", object_list<1>));
    runtime.register_sqfop(nular("allDeadMen", "All dead units.", object_list<2>));
    runtime.register_sqfop(nular("allPlayers", "SQF-VM has one player.", player_list));
    runtime.register_sqfop(nular("playableUnits", "SQF-VM has one player.", player_list));
    runtime.register_sqfop(nular("switchableUnits", "SQF-VM has one player.", player_list));
    runtime.register_sqfop(binary(4, "inArea", t_any(), t_any(), "True if the position or object is in the area ([center, a, b, angle, isRectangle, c] or a marker).", inarea_any_any));
    runtime.register_sqfop(binary(4, "inAreaArray", t_array(), t_any(), "The positions or objects that are in the area.", inareaarray_array_any<false>));
    runtime.register_sqfop(binary(4, "inAreaArrayIndexes", t_array(), t_any(), "Indexes of the positions or objects that are in the area.", inareaarray_array_any<true>));
}
