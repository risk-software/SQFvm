#include "ops_inventory.h"
#include "object.h"
#include "d_object.h"
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
#include <cctype>
#include <string>
#include <vector>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    // Rounds of a magazine whose class is not in CfgMagazines.
    constexpr int unconfigured_magazine_rounds = 30;

    std::string lowercase(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }
    bool starts_with(const std::string& text, const char* prefix)
    {
        return lowercase(text).rfind(prefix, 0) == 0;
    }
    bool contains(const std::string& text, const char* part)
    {
        return lowercase(text).find(part) != std::string::npos;
    }

    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
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
    std::shared_ptr<object> require_object(runtime& runtime, value::cref source)
    {
        auto obj = object_of(source);
        if (!obj)
        {
            runtime.__logmsg(err::ExpectedNonNullValueWeak(position(runtime)));
        }
        return obj;
    }
    unit_inventory& inventory_of(const std::shared_ptr<object>& obj)
    {
        auto& slot = obj->world().inventory;
        if (!slot)
        {
            slot = std::make_shared<unit_inventory>();
        }
        return *slot;
    }

    // ---------------------------------------------------------------- classification

    enum class item_kind { primary, handgun, secondary, binocular, uniform, vest, headgear, goggles, backpack, magazine, map, gps, radio, compass, watch, nvg, optic, pointer, muzzle, bipod, item };

    confignav config_class(runtime& runtime, const char* root, const std::string& name)
    {
        auto nav = runtime.confighost().root() / root;
        if (nav.empty())
        {
            return nav;
        }
        return nav / name;
    }
    float config_number(const confignav& nav, const char* entry, float fallback)
    {
        if (nav.empty())
        {
            return fallback;
        }
        auto found = nav / entry;
        return found.empty() ? fallback : found->value.data_try<d_scalar, float>(fallback);
    }
    std::string config_text(const confignav& nav, const char* entry)
    {
        if (nav.empty())
        {
            return {};
        }
        auto found = nav / entry;
        return found.empty() ? std::string() : found->value.data_try<d_string, std::string>(std::string());
    }

    /// Kind of an item, from the loaded config when it has the class, otherwise from the class-name
    /// prefixes Bohemia uses (arifle_, hgun_, launch_, U_, V_, H_, G_, optic_, ...).
    item_kind classify(runtime& runtime, const std::string& name)
    {
        if (!config_class(runtime, "CfgMagazines", name).empty())
        {
            return item_kind::magazine;
        }
        auto vehicle = config_class(runtime, "CfgVehicles", name);
        if (!vehicle.empty() && config_number(vehicle, "isBackpack", 0) == 1)
        {
            return item_kind::backpack;
        }
        if (!config_class(runtime, "CfgGlasses", name).empty())
        {
            return item_kind::goggles;
        }
        auto weapon = config_class(runtime, "CfgWeapons", name);
        if (!weapon.empty())
        {
            auto simulation = lowercase(config_text(weapon, "simulation"));
            if (simulation == "itemmap") return item_kind::map;
            if (simulation == "itemgps") return item_kind::gps;
            if (simulation == "itemradio") return item_kind::radio;
            if (simulation == "itemcompass") return item_kind::compass;
            if (simulation == "itemwatch") return item_kind::watch;
            if (simulation == "nvgoggles") return item_kind::nvg;
            auto info = weapon / "ItemInfo";
            switch (static_cast<int>(config_number(info, "type", -1)))
            {
            case 801: return item_kind::uniform;
            case 701: return item_kind::vest;
            case 605: return item_kind::headgear;
            case 616: return item_kind::nvg;
            case 201: return item_kind::optic;
            case 301: return item_kind::pointer;
            case 101: return item_kind::muzzle;
            case 302: return item_kind::bipod;
            }
            switch (static_cast<int>(config_number(weapon, "type", -1)))
            {
            case 1: return item_kind::primary;
            case 2: return item_kind::handgun;
            case 4: return item_kind::secondary;
            case 4096: return item_kind::binocular;
            }
            return item_kind::item;
        }
        if (starts_with(name, "itemmap")) return item_kind::map;
        if (starts_with(name, "itemgps") || contains(name, "_uavterminal")) return item_kind::gps;
        if (starts_with(name, "itemradio") || starts_with(name, "tf_") || starts_with(name, "acre_")) return item_kind::radio;
        if (starts_with(name, "itemcompass")) return item_kind::compass;
        if (starts_with(name, "itemwatch")) return item_kind::watch;
        if (starts_with(name, "nvgoggles") || starts_with(name, "nvg")) return item_kind::nvg;
        if (starts_with(name, "u_")) return item_kind::uniform;
        if (starts_with(name, "v_")) return item_kind::vest;
        if (starts_with(name, "h_")) return item_kind::headgear;
        if (starts_with(name, "g_")) return item_kind::goggles;
        if (starts_with(name, "optic_")) return item_kind::optic;
        if (starts_with(name, "acc_")) return item_kind::pointer;
        if (starts_with(name, "muzzle_")) return item_kind::muzzle;
        if (starts_with(name, "bipod_")) return item_kind::bipod;
        if (starts_with(name, "b_") && (contains(name, "pack") || contains(name, "kitbag") || contains(name, "carryall") || contains(name, "bergen"))) return item_kind::backpack;
        if (starts_with(name, "launch_")) return item_kind::secondary;
        if (starts_with(name, "hgun_pdw") || starts_with(name, "hgun_pdw")) return item_kind::primary;
        if (starts_with(name, "hgun_")) return item_kind::handgun;
        if (starts_with(name, "binocular") || starts_with(name, "rangefinder") || starts_with(name, "laserdesignator")) return item_kind::binocular;
        if (starts_with(name, "arifle_") || starts_with(name, "srifle_") || starts_with(name, "lmg_") || starts_with(name, "mmg_") || starts_with(name, "smg_") || starts_with(name, "sgun_")) return item_kind::primary;
        if (contains(name, "rnd_") || contains(name, "_mag") || contains(name, "grenade") || contains(name, "smokeshell") || contains(name, "chemlight") || contains(name, "_mine")) return item_kind::magazine;
        return item_kind::item;
    }
    int assigned_slot(item_kind kind)
    {
        switch (kind)
        {
        case item_kind::map: return 0;
        case item_kind::gps: return 1;
        case item_kind::radio: return 2;
        case item_kind::compass: return 3;
        case item_kind::watch: return 4;
        case item_kind::nvg: return 5;
        default: return -1;
        }
    }
    int magazine_rounds(runtime& runtime, const std::string& name)
    {
        return static_cast<int>(config_number(config_class(runtime, "CfgMagazines", name), "count", unconfigured_magazine_rounds));
    }

    // ---------------------------------------------------------------- loadout arrays

    value text(const std::string& s) { return s; }
    value empty_array() { return std::make_shared<d_array>(); }

    value weapon_value(const weapon_slot& slot)
    {
        if (slot.empty())
        {
            return empty_array();
        }
        auto magazine = [](const std::string& name, int ammo) -> value
        {
            return name.empty() ? empty_array() : value(std::make_shared<d_array>(std::vector<value>{ name, static_cast<float>(ammo) }));
        };
        return std::make_shared<d_array>(std::vector<value>{
            slot.weapon, slot.muzzle, slot.pointer, slot.optic,
            magazine(slot.magazine, slot.magazine_ammo), magazine(slot.secondary_magazine, slot.secondary_magazine_ammo), slot.bipod });
    }
    value container_value(const item_container& container)
    {
        if (container.empty())
        {
            return empty_array();
        }
        std::vector<value> contents;
        for (auto& entry : container.contents)
        {
            std::vector<value> item{ entry.class_name, static_cast<float>(entry.count) };
            if (entry.ammo >= 0)
            {
                item.push_back(static_cast<float>(entry.ammo));
            }
            contents.push_back(value(std::make_shared<d_array>(item)));
        }
        return std::make_shared<d_array>(std::vector<value>{ container.class_name, value(std::make_shared<d_array>(contents)) });
    }
    value loadout_value(const unit_inventory& inv)
    {
        std::vector<value> assigned(inv.assigned.begin(), inv.assigned.end());
        return std::make_shared<d_array>(std::vector<value>{
            weapon_value(inv.primary), weapon_value(inv.secondary), weapon_value(inv.handgun),
            container_value(inv.uniform), container_value(inv.vest), container_value(inv.backpack),
            inv.headgear, inv.goggles, weapon_value(inv.binocular), value(std::make_shared<d_array>(assigned)) });
    }

    std::string string_at(const std::shared_ptr<d_array>& arr, size_t index)
    {
        return index < arr->size() ? arr->at(index).data_try<d_string, std::string>(std::string()) : std::string();
    }
    weapon_slot weapon_from(value::cref source)
    {
        weapon_slot slot;
        auto arr = source.data_try<d_array>();
        if (!arr || arr->empty())
        {
            return slot;
        }
        slot.weapon = string_at(arr, 0);
        slot.muzzle = string_at(arr, 1);
        slot.pointer = string_at(arr, 2);
        slot.optic = string_at(arr, 3);
        auto read_magazine = [&arr](size_t index, std::string& name, int& ammo)
        {
            auto magazine = index < arr->size() ? arr->at(index).data_try<d_array>() : nullptr;
            if (magazine && magazine->size() >= 1)
            {
                name = string_at(magazine, 0);
                ammo = magazine->size() >= 2 ? static_cast<int>(magazine->at(1).data_try<d_scalar, float>(0)) : 0;
            }
        };
        read_magazine(4, slot.magazine, slot.magazine_ammo);
        read_magazine(5, slot.secondary_magazine, slot.secondary_magazine_ammo);
        slot.bipod = string_at(arr, 6);
        return slot;
    }
    item_container container_from(value::cref source)
    {
        item_container container;
        auto arr = source.data_try<d_array>();
        if (!arr || arr->empty())
        {
            return container;
        }
        container.class_name = string_at(arr, 0);
        auto contents = arr->size() >= 2 ? arr->at(1).data_try<d_array>() : nullptr;
        if (contents)
        {
            for (auto& entry : contents->value())
            {
                auto item = entry.data_try<d_array>();
                if (!item || item->size() < 2)
                {
                    continue;
                }
                container.contents.push_back({ string_at(item, 0),
                    static_cast<int>(item->at(1).data_try<d_scalar, float>(1)),
                    item->size() >= 3 ? static_cast<int>(item->at(2).data_try<d_scalar, float>(0)) : -1 });
            }
        }
        return container;
    }
    bool loadout_from(runtime& runtime, value::cref source, unit_inventory& inv)
    {
        auto arr = source.data_try<d_array>();
        if (!arr || arr->size() != 10)
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "setUnitLoadout", "expected a loadout array with 10 elements, as getUnitLoadout returns"));
            return false;
        }
        inv.primary = weapon_from(arr->at(0));
        inv.secondary = weapon_from(arr->at(1));
        inv.handgun = weapon_from(arr->at(2));
        inv.uniform = container_from(arr->at(3));
        inv.vest = container_from(arr->at(4));
        inv.backpack = container_from(arr->at(5));
        inv.headgear = string_at(arr, 6);
        inv.goggles = string_at(arr, 7);
        inv.binocular = weapon_from(arr->at(8));
        auto assigned = arr->at(9).data_try<d_array>();
        inv.assigned = {};
        for (size_t i = 0; assigned && i < std::min<size_t>(6, assigned->size()); i++)
        {
            inv.assigned[i] = string_at(assigned, i);
        }
        return true;
    }
    void refill_magazines(runtime& runtime, unit_inventory& inv)
    {
        for (auto* slot : { &inv.primary, &inv.secondary, &inv.handgun, &inv.binocular })
        {
            if (!slot->magazine.empty()) { slot->magazine_ammo = magazine_rounds(runtime, slot->magazine); }
            if (!slot->secondary_magazine.empty()) { slot->secondary_magazine_ammo = magazine_rounds(runtime, slot->secondary_magazine); }
        }
        for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
        {
            for (auto& entry : container->contents)
            {
                if (entry.ammo >= 0) { entry.ammo = magazine_rounds(runtime, entry.class_name); }
            }
        }
    }

    // ---------------------------------------------------------------- containers

    item_container* first_container(unit_inventory& inv)
    {
        for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
        {
            if (!container->empty())
            {
                return container;
            }
        }
        return nullptr;
    }
    void add_to(item_container& container, const std::string& name, int count, int ammo)
    {
        for (auto& entry : container.contents)
        {
            if (entry.class_name == name && entry.ammo == ammo)
            {
                entry.count += count;
                return;
            }
        }
        container.contents.push_back({ name, count, ammo });
    }
    /// Removes one of the class; magazines match any ammo unless `ammo` is given.
    bool remove_from(item_container& container, const std::string& name, int ammo = -2)
    {
        for (auto it = container.contents.begin(); it != container.contents.end(); ++it)
        {
            if (lowercase(it->class_name) == lowercase(name) && (ammo == -2 || it->ammo == ammo))
            {
                if (--it->count <= 0)
                {
                    container.contents.erase(it);
                }
                return true;
            }
        }
        return false;
    }
    /// Puts a magazine or item into the first container that exists: uniform, vest, then backpack.
    bool store_item(runtime& runtime, unit_inventory& inv, const std::string& name, int ammo_override = -1)
    {
        auto* container = first_container(inv);
        if (!container)
        {
            return false;
        }
        bool magazine = classify(runtime, name) == item_kind::magazine;
        add_to(*container, name, 1, magazine ? (ammo_override >= 0 ? ammo_override : magazine_rounds(runtime, name)) : -1);
        return true;
    }

    std::vector<value> expanded(const std::vector<container_entry>& entries, bool magazines)
    {
        std::vector<value> out;
        for (auto& entry : entries)
        {
            if ((entry.ammo >= 0) != magazines)
            {
                continue;
            }
            for (int i = 0; i < entry.count; i++)
            {
                out.push_back(entry.class_name);
            }
        }
        return out;
    }

    weapon_slot* slot_for(unit_inventory& inv, item_kind kind)
    {
        switch (kind)
        {
        case item_kind::primary: return &inv.primary;
        case item_kind::handgun: return &inv.handgun;
        case item_kind::secondary: return &inv.secondary;
        case item_kind::binocular: return &inv.binocular;
        default: return nullptr;
        }
    }

    // ---------------------------------------------------------------- commands

    value getunitloadout_object(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        if (!obj)
        {
            return {};
        }
        return loadout_value(inventory_of(obj));
    }

    /// The loadout of a CfgVehicles class from its uniformClass, backpack, linkedItems[], weapons[],
    /// magazines[] and items[] entries.
    bool loadout_from_config(runtime& runtime, const confignav& nav, unit_inventory& inv);

    value setunitloadout_object_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        unit_inventory loaded;
        bool full_magazines = false;
        bool ok = false;
        if (right.is<t_array>() && right.data<d_array>()->size() == 2 && right.data<d_array>()->at(0).is<t_array>())
        {
            ok = loadout_from(runtime, right.data<d_array>()->at(0), loaded);
            full_magazines = right.data<d_array>()->at(1).data_try<d_boolean, bool>(false);
        }
        else if (right.is<t_array>())
        {
            ok = loadout_from(runtime, right, loaded);
        }
        else if (right.is<t_string>())
        {
            ok = loadout_from_config(runtime, config_class(runtime, "CfgVehicles", right.data<d_string, std::string>()), loaded);
        }
        else if (right.is<t_config>())
        {
            ok = loadout_from_config(runtime, right.data<d_config, config>().navigate(runtime.confighost()), loaded);
        }
        if (!ok)
        {
            return {};
        }
        if (full_magazines)
        {
            refill_magazines(runtime, loaded);
        }
        auto& inv = inventory_of(obj);
        loaded.cargo_items = inv.cargo_items;
        loaded.cargo_weapons = inv.cargo_weapons;
        loaded.cargo_magazines = inv.cargo_magazines;
        loaded.cargo_backpacks = inv.cargo_backpacks;
        inv = loaded;
        return {};
    }

    template<int Slot>
    value weapon_getter(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        if (!obj)
        {
            return std::string();
        }
        auto& inv = inventory_of(obj);
        const weapon_slot* slots[] = { &inv.primary, &inv.secondary, &inv.handgun, &inv.binocular };
        return slots[Slot]->weapon;
    }
    template<int Slot>
    value weapon_items_getter(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        if (!obj)
        {
            return empty_array();
        }
        auto& inv = inventory_of(obj);
        const weapon_slot* slots[] = { &inv.primary, &inv.secondary, &inv.handgun };
        auto slot = slots[Slot];
        return std::make_shared<d_array>(std::vector<value>{ slot->muzzle, slot->pointer, slot->optic, slot->bipod });
    }
    template<int Slot>
    value weapon_magazine_getter(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        std::vector<value> out;
        if (obj)
        {
            auto& inv = inventory_of(obj);
            const weapon_slot* slots[] = { &inv.primary, &inv.secondary, &inv.handgun };
            auto slot = slots[Slot];
            if (!slot->magazine.empty()) { out.push_back(slot->magazine); }
            if (!slot->secondary_magazine.empty()) { out.push_back(slot->secondary_magazine); }
        }
        return std::make_shared<d_array>(out);
    }
    /// Attachments go to their slot, magazines are loaded.
    template<int Slot>
    value add_weapon_item(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto& inv = inventory_of(obj);
        weapon_slot* slots[] = { &inv.primary, &inv.secondary, &inv.handgun };
        auto slot = slots[Slot];
        if (slot->empty())
        {
            return {};
        }
        auto name = right.data<d_string, std::string>();
        switch (classify(runtime, name))
        {
        case item_kind::optic: slot->optic = name; break;
        case item_kind::pointer: slot->pointer = name; break;
        case item_kind::muzzle: slot->muzzle = name; break;
        case item_kind::bipod: slot->bipod = name; break;
        case item_kind::magazine: slot->magazine = name; slot->magazine_ammo = magazine_rounds(runtime, name); break;
        default: slot->optic = name; break;
        }
        return {};
    }
    template<int Slot>
    value remove_weapon_item(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto& inv = inventory_of(obj);
        weapon_slot* slots[] = { &inv.primary, &inv.secondary, &inv.handgun };
        auto slot = slots[Slot];
        auto name = lowercase(right.data<d_string, std::string>());
        for (auto* field : { &slot->muzzle, &slot->pointer, &slot->optic, &slot->bipod })
        {
            if (lowercase(*field) == name) { field->clear(); }
        }
        if (lowercase(slot->magazine) == name) { slot->magazine.clear(); slot->magazine_ammo = 0; }
        if (lowercase(slot->secondary_magazine) == name) { slot->secondary_magazine.clear(); slot->secondary_magazine_ammo = 0; }
        return {};
    }

    template<int Gear>
    value gear_getter(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        if (!obj)
        {
            return std::string();
        }
        auto& inv = inventory_of(obj);
        switch (Gear)
        {
        case 0: return inv.uniform.class_name;
        case 1: return inv.vest.class_name;
        case 2: return inv.backpack.class_name;
        case 3: return inv.headgear;
        case 4: return inv.goggles;
        default: return inv.assigned[5];
        }
    }
    template<int Gear>
    value gear_add(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto& inv = inventory_of(obj);
        auto name = right.data<d_string, std::string>();
        switch (Gear)
        {
        case 0: inv.uniform = { name, {} }; break;
        case 1: inv.vest = { name, {} }; break;
        case 2: inv.backpack = { name, {} }; break;
        case 3: inv.headgear = name; break;
        case 4: inv.goggles = name; break;
        }
        return {};
    }
    template<int Gear>
    value gear_remove(runtime& runtime, value::cref right)
    {
        auto obj = require_object(runtime, right);
        if (!obj)
        {
            return {};
        }
        auto& inv = inventory_of(obj);
        switch (Gear)
        {
        case 0: inv.uniform = {}; break;
        case 1: inv.vest = {}; break;
        case 2: inv.backpack = {}; break;
        case 3: inv.headgear.clear(); break;
        case 4: inv.goggles.clear(); break;
        }
        return {};
    }

    void add_weapon(runtime& runtime, unit_inventory& inv, const std::string& name)
    {
        auto slot = slot_for(inv, classify(runtime, name));
        if (!slot)
        {
            // Items given to addWeapon go into the assigned slot or a container, as the engine does.
            auto assigned = assigned_slot(classify(runtime, name));
            if (assigned >= 0) { inv.assigned[assigned] = name; }
            else { store_item(runtime, inv, name); }
            return;
        }
        *slot = {};
        slot->weapon = name;
        // A compatible magazine in the containers is loaded, as when a unit picks up a weapon.
        for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
        {
            for (auto& entry : container->contents)
            {
                if (entry.ammo >= 0)
                {
                    auto weapon_config = config_class(runtime, "CfgWeapons", name);
                    auto magazines = weapon_config.empty() ? confignav(weapon_config) : weapon_config / "magazines";
                    bool compatible = false;
                    if (!magazines.empty())
                    {
                        if (auto list = magazines->value.data_try<d_array>())
                        {
                            for (auto& entry_name : list->value())
                            {
                                compatible = compatible || lowercase(entry_name.data_try<d_string, std::string>(std::string())) == lowercase(entry.class_name);
                            }
                        }
                    }
                    if (compatible)
                    {
                        slot->magazine = entry.class_name;
                        slot->magazine_ammo = entry.ammo;
                        remove_from(*container, entry.class_name, entry.ammo);
                        return;
                    }
                }
            }
        }
    }
    value addweapon_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        if (auto obj = require_object(runtime, left))
        {
            add_weapon(runtime, inventory_of(obj), right.data<d_string, std::string>());
        }
        return {};
    }
    /// object addWeapon [weapon, autoselect] (2.20)
    value addweapon_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto params = right.data<d_array>();
        if (auto obj = require_object(runtime, left); obj && params->size() >= 1 && params->at(0).is<t_string>())
        {
            add_weapon(runtime, inventory_of(obj), params->at(0).data<d_string, std::string>());
        }
        return {};
    }
    void remove_weapon(unit_inventory& inv, const std::string& name)
    {
        for (auto* slot : { &inv.primary, &inv.secondary, &inv.handgun, &inv.binocular })
        {
            if (lowercase(slot->weapon) == lowercase(name))
            {
                *slot = {};
                return;
            }
        }
        for (auto& assigned : inv.assigned)
        {
            if (lowercase(assigned) == lowercase(name))
            {
                assigned.clear();
                return;
            }
        }
    }
    value removeweapon_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        if (auto obj = require_object(runtime, left))
        {
            remove_weapon(inventory_of(obj), right.data<d_string, std::string>());
        }
        return {};
    }
    value removeweapon_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto params = right.data<d_array>();
        if (auto obj = require_object(runtime, left); obj && params->size() >= 1)
        {
            remove_weapon(inventory_of(obj), params->at(0).data_try<d_string, std::string>(std::string()));
        }
        return {};
    }
    value weapons_object(runtime& runtime, value::cref right)
    {
        std::vector<value> out;
        if (auto obj = object_of(right))
        {
            auto& inv = inventory_of(obj);
            for (auto* slot : { &inv.primary, &inv.secondary, &inv.handgun, &inv.binocular })
            {
                if (!slot->empty()) { out.push_back(slot->weapon); }
            }
        }
        return std::make_shared<d_array>(out);
    }
    value removeallweapons_object(runtime& runtime, value::cref right)
    {
        if (auto obj = require_object(runtime, right))
        {
            auto& inv = inventory_of(obj);
            inv.primary = {};
            inv.secondary = {};
            inv.handgun = {};
            inv.binocular = {};
            // Magazines go with the weapons, as in the engine.
            for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
            {
                auto& contents = container->contents;
                contents.erase(std::remove_if(contents.begin(), contents.end(), [](const container_entry& e) { return e.ammo >= 0; }), contents.end());
            }
        }
        return {};
    }

    value addmagazine_object_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        if (right.is<t_string>())
        {
            store_item(runtime, inventory_of(obj), right.data<d_string, std::string>());
        }
        else if (right.is<t_array>() && right.data<d_array>()->size() == 2)
        {
            auto params = right.data<d_array>();
            store_item(runtime, inventory_of(obj), params->at(0).data_try<d_string, std::string>(std::string()), static_cast<int>(params->at(1).data_try<d_scalar, float>(0)));
        }
        return {};
    }
    /// unit addMagazines [magazine, count]
    value addmagazines_object_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() != 2)
        {
            return {};
        }
        auto name = params->at(0).data_try<d_string, std::string>(std::string());
        for (int i = 0; i < static_cast<int>(params->at(1).data_try<d_scalar, float>(0)); i++)
        {
            store_item(runtime, inventory_of(obj), name);
        }
        return {};
    }
    std::vector<container_entry> all_contents(unit_inventory& inv)
    {
        std::vector<container_entry> out;
        for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
        {
            out.insert(out.end(), container->contents.begin(), container->contents.end());
        }
        return out;
    }
    value magazines_object(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        return std::make_shared<d_array>(obj ? expanded(all_contents(inventory_of(obj)), true) : std::vector<value>{});
    }
    /// magazinesAmmo unit: [[magazine, ammo], ...] of the magazines in the containers.
    value magazinesammo_object(runtime& runtime, value::cref right)
    {
        std::vector<value> out;
        if (auto obj = object_of(right))
        {
            for (auto& entry : all_contents(inventory_of(obj)))
            {
                for (int i = 0; entry.ammo >= 0 && i < entry.count; i++)
                {
                    out.push_back(value(std::make_shared<d_array>(std::vector<value>{ entry.class_name, static_cast<float>(entry.ammo) })));
                }
            }
        }
        return std::make_shared<d_array>(out);
    }
    value removemagazine_object_any(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto& inv = inventory_of(obj);
        std::string name = right.is<t_string>() ? right.data<d_string, std::string>() : (right.is<t_array>() ? string_at(right.data<d_array>(), 0) : std::string());
        int ammo = right.is<t_array>() && right.data<d_array>()->size() >= 2 ? static_cast<int>(right.data<d_array>()->at(1).data_try<d_scalar, float>(0)) : -2;
        for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
        {
            if (remove_from(*container, name, ammo))
            {
                return {};
            }
        }
        return {};
    }
    value removemagazines_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        if (auto obj = require_object(runtime, left))
        {
            auto name = lowercase(right.data<d_string, std::string>());
            auto& inv = inventory_of(obj);
            for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
            {
                auto& contents = container->contents;
                contents.erase(std::remove_if(contents.begin(), contents.end(), [&name](const container_entry& e) { return e.ammo >= 0 && lowercase(e.class_name) == name; }), contents.end());
            }
        }
        return {};
    }

    value additem_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        if (auto obj = require_object(runtime, left))
        {
            store_item(runtime, inventory_of(obj), right.data<d_string, std::string>());
        }
        return {};
    }
    value removeitem_object_string(runtime& runtime, value::cref left, value::cref right)
    {
        if (auto obj = require_object(runtime, left))
        {
            auto& inv = inventory_of(obj);
            auto name = right.data<d_string, std::string>();
            for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
            {
                if (remove_from(*container, name))
                {
                    return {};
                }
            }
        }
        return {};
    }
    value items_object(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        return std::make_shared<d_array>(obj ? expanded(all_contents(inventory_of(obj)), false) : std::vector<value>{});
    }
    value removeallitems_object(runtime& runtime, value::cref right)
    {
        if (auto obj = require_object(runtime, right))
        {
            auto& inv = inventory_of(obj);
            for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
            {
                auto& contents = container->contents;
                contents.erase(std::remove_if(contents.begin(), contents.end(), [](const container_entry& e) { return e.ammo < 0; }), contents.end());
            }
        }
        return {};
    }
    /// assignItem moves an item from a container into its slot; linkItem puts it there directly.
    value assign_item(runtime& runtime, value::cref left, value::cref right, bool from_container)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto& inv = inventory_of(obj);
        auto name = right.data<d_string, std::string>();
        auto slot = assigned_slot(classify(runtime, name));
        if (slot < 0)
        {
            return {};
        }
        if (from_container)
        {
            bool found = false;
            for (auto* container : { &inv.uniform, &inv.vest, &inv.backpack })
            {
                if (remove_from(*container, name)) { found = true; break; }
            }
            if (!found)
            {
                return {};
            }
        }
        if (!inv.assigned[slot].empty() && from_container)
        {
            store_item(runtime, inv, inv.assigned[slot]);
        }
        inv.assigned[slot] = name;
        return {};
    }
    /// unassignItem moves the item back into a container; unlinkItem removes it.
    value unassign_item(runtime& runtime, value::cref left, value::cref right, bool to_container)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto& inv = inventory_of(obj);
        auto name = lowercase(right.data<d_string, std::string>());
        for (auto& assigned : inv.assigned)
        {
            if (lowercase(assigned) == name)
            {
                if (to_container) { store_item(runtime, inv, assigned); }
                assigned.clear();
                break;
            }
        }
        return {};
    }
    value assigneditems_object(runtime& runtime, value::cref right)
    {
        std::vector<value> out;
        if (auto obj = object_of(right))
        {
            auto& inv = inventory_of(obj);
            for (auto& assigned : inv.assigned)
            {
                if (!assigned.empty()) { out.push_back(assigned); }
            }
            if (!inv.binocular.empty()) { out.push_back(inv.binocular.weapon); }
        }
        return std::make_shared<d_array>(out);
    }
    value removeallassigneditems_object(runtime& runtime, value::cref right)
    {
        if (auto obj = require_object(runtime, right))
        {
            inventory_of(obj).assigned = {};
        }
        return {};
    }

    template<int Container>
    item_container& container_at(unit_inventory& inv)
    {
        item_container* containers[] = { &inv.uniform, &inv.vest, &inv.backpack };
        return *containers[Container];
    }
    template<int Container>
    value add_item_to_container(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        if (!obj)
        {
            return {};
        }
        auto& container = container_at<Container>(inventory_of(obj));
        if (container.empty())
        {
            return {};
        }
        auto name = right.data<d_string, std::string>();
        bool magazine = classify(runtime, name) == item_kind::magazine;
        add_to(container, name, 1, magazine ? magazine_rounds(runtime, name) : -1);
        return {};
    }
    template<int Container>
    value remove_item_from_container(runtime& runtime, value::cref left, value::cref right)
    {
        if (auto obj = require_object(runtime, left))
        {
            remove_from(container_at<Container>(inventory_of(obj)), right.data<d_string, std::string>());
        }
        return {};
    }
    template<int Container, bool Magazines>
    value container_contents(runtime& runtime, value::cref right)
    {
        auto obj = object_of(right);
        if (!obj)
        {
            return empty_array();
        }
        auto& container = container_at<Container>(inventory_of(obj));
        if (Magazines)
        {
            return std::make_shared<d_array>(expanded(container.contents, true));
        }
        // uniformItems and the like list magazines too.
        auto out = expanded(container.contents, false);
        auto magazines = expanded(container.contents, true);
        out.insert(out.end(), magazines.begin(), magazines.end());
        return std::make_shared<d_array>(out);
    }
    template<int Container>
    value clear_container(runtime& runtime, value::cref right)
    {
        if (auto obj = require_object(runtime, right))
        {
            container_at<Container>(inventory_of(obj)).contents.clear();
        }
        return {};
    }

    bool loadout_from_config(runtime& runtime, const confignav& nav, unit_inventory& inv)
    {
        if (nav.empty())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "setUnitLoadout", "the class is not in CfgVehicles"));
            return false;
        }
        auto list = [&nav](const char* entry) -> std::vector<std::string>
        {
            std::vector<std::string> out;
            auto found = nav / entry;
            if (!found.empty())
            {
                if (auto arr = found->value.data_try<d_array>())
                {
                    for (auto& item : arr->value()) { out.push_back(item.data_try<d_string, std::string>(std::string())); }
                }
            }
            return out;
        };
        inv = {};
        auto uniform = config_text(nav, "uniformClass");
        if (!uniform.empty()) { inv.uniform = { uniform, {} }; }
        auto backpack = config_text(nav, "backpack");
        if (!backpack.empty()) { inv.backpack = { backpack, {} }; }
        for (auto& item : list("linkedItems"))
        {
            auto kind = classify(runtime, item);
            if (kind == item_kind::vest) { inv.vest = { item, {} }; }
            else if (kind == item_kind::headgear) { inv.headgear = item; }
            else if (kind == item_kind::goggles) { inv.goggles = item; }
            else if (auto slot = assigned_slot(kind); slot >= 0) { inv.assigned[slot] = item; }
        }
        for (auto& magazine : list("magazines")) { store_item(runtime, inv, magazine); }
        for (auto& weapon : list("weapons"))
        {
            if (lowercase(weapon) != "throw" && lowercase(weapon) != "put") { add_weapon(runtime, inv, weapon); }
        }
        for (auto& item : list("items")) { store_item(runtime, inv, item); }
        return true;
    }

    // ---------------------------------------------------------------- vehicle cargo

    template<int Kind>
    std::vector<container_entry>& cargo_list(unit_inventory& inv)
    {
        std::vector<container_entry>* lists[] = { &inv.cargo_items, &inv.cargo_weapons, &inv.cargo_magazines, &inv.cargo_backpacks };
        return *lists[Kind];
    }
    /// vehicle addItemCargo [class, count] and the weapon, magazine and backpack forms.
    template<int Kind>
    value add_cargo(runtime& runtime, value::cref left, value::cref right)
    {
        auto obj = require_object(runtime, left);
        auto params = right.data<d_array>();
        if (!obj || params->size() != 2 || !params->at(0).is<t_string>() || !params->at(1).is<t_scalar>())
        {
            return {};
        }
        auto name = params->at(0).data<d_string, std::string>();
        auto count = static_cast<int>(params->at(1).data<d_scalar, float>());
        auto& list = cargo_list<Kind>(inventory_of(obj));
        auto ammo = Kind == 2 ? magazine_rounds(runtime, name) : -1;
        for (auto& entry : list)
        {
            if (entry.class_name == name && entry.ammo == ammo)
            {
                entry.count += count;
                return {};
            }
        }
        list.push_back({ name, count, ammo });
        return {};
    }
    /// getItemCargo and the like: [[classes], [counts]].
    template<int Kind>
    value get_cargo(runtime& runtime, value::cref right)
    {
        std::vector<value> names;
        std::vector<value> counts;
        if (auto obj = object_of(right))
        {
            for (auto& entry : cargo_list<Kind>(inventory_of(obj)))
            {
                names.push_back(entry.class_name);
                counts.push_back(static_cast<float>(entry.count));
            }
        }
        return std::make_shared<d_array>(std::vector<value>{ value(std::make_shared<d_array>(names)), value(std::make_shared<d_array>(counts)) });
    }
    /// itemCargo and the like: one class name per item.
    template<int Kind>
    value list_cargo(runtime& runtime, value::cref right)
    {
        std::vector<value> out;
        if (auto obj = object_of(right))
        {
            for (auto& entry : cargo_list<Kind>(inventory_of(obj)))
            {
                for (int i = 0; i < entry.count; i++) { out.push_back(entry.class_name); }
            }
        }
        return std::make_shared<d_array>(out);
    }
    template<int Kind>
    value clear_cargo(runtime& runtime, value::cref right)
    {
        if (auto obj = require_object(runtime, right))
        {
            cargo_list<Kind>(inventory_of(obj)).clear();
        }
        return {};
    }
}

void sqf::operators::ops_inventory(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(unary("getUnitLoadout", t_object(), "The unit's loadout as [primary, secondary, handgun, uniform, vest, backpack, headgear, goggles, binocular, assignedItems].", getunitloadout_object));
    runtime.register_sqfop(binary(4, "setUnitLoadout", t_object(), t_any(), "Sets the loadout from an array, [array, fullMagazines], a CfgVehicles class name or a config.", setunitloadout_object_any));

    runtime.register_sqfop(unary("primaryWeapon", t_object(), "Class of the primary weapon, or \"\".", weapon_getter<0>));
    runtime.register_sqfop(unary("secondaryWeapon", t_object(), "Class of the launcher, or \"\".", weapon_getter<1>));
    runtime.register_sqfop(unary("handgunWeapon", t_object(), "Class of the handgun, or \"\".", weapon_getter<2>));
    runtime.register_sqfop(unary("binocular", t_object(), "Class of the binocular, or \"\".", weapon_getter<3>));
    runtime.register_sqfop(unary("primaryWeaponItems", t_object(), "[muzzle, pointer, optic, bipod] of the primary weapon.", weapon_items_getter<0>));
    runtime.register_sqfop(unary("secondaryWeaponItems", t_object(), "[muzzle, pointer, optic, bipod] of the launcher.", weapon_items_getter<1>));
    runtime.register_sqfop(unary("handgunItems", t_object(), "[muzzle, pointer, optic, bipod] of the handgun.", weapon_items_getter<2>));
    runtime.register_sqfop(unary("primaryWeaponMagazine", t_object(), "Magazines loaded in the primary weapon.", weapon_magazine_getter<0>));
    runtime.register_sqfop(unary("secondaryWeaponMagazine", t_object(), "Magazines loaded in the launcher.", weapon_magazine_getter<1>));
    runtime.register_sqfop(unary("handgunMagazine", t_object(), "Magazines loaded in the handgun.", weapon_magazine_getter<2>));
    runtime.register_sqfop(binary(4, "addPrimaryWeaponItem", t_object(), t_string(), "Adds an attachment or loads a magazine.", add_weapon_item<0>));
    runtime.register_sqfop(binary(4, "addSecondaryWeaponItem", t_object(), t_string(), "Adds an attachment or loads a magazine.", add_weapon_item<1>));
    runtime.register_sqfop(binary(4, "addHandgunItem", t_object(), t_string(), "Adds an attachment or loads a magazine.", add_weapon_item<2>));
    runtime.register_sqfop(binary(4, "removePrimaryWeaponItem", t_object(), t_string(), "Removes an attachment or magazine.", remove_weapon_item<0>));
    runtime.register_sqfop(binary(4, "removeSecondaryWeaponItem", t_object(), t_string(), "Removes an attachment or magazine.", remove_weapon_item<1>));
    runtime.register_sqfop(binary(4, "removeHandgunItem", t_object(), t_string(), "Removes an attachment or magazine.", remove_weapon_item<2>));

    runtime.register_sqfop(unary("uniform", t_object(), "Class of the uniform, or \"\".", gear_getter<0>));
    runtime.register_sqfop(unary("vest", t_object(), "Class of the vest, or \"\".", gear_getter<1>));
    runtime.register_sqfop(unary("backpack", t_object(), "Class of the backpack, or \"\".", gear_getter<2>));
    runtime.register_sqfop(unary("headgear", t_object(), "Class of the headgear, or \"\".", gear_getter<3>));
    runtime.register_sqfop(unary("goggles", t_object(), "Class of the goggles, or \"\".", gear_getter<4>));
    runtime.register_sqfop(unary("hmd", t_object(), "Class of the night vision or head-mounted display, or \"\".", gear_getter<5>));
    for (auto name : { "addUniform", "forceAddUniform" }) { runtime.register_sqfop(binary(4, name, t_object(), t_string(), "Puts on the uniform; its contents are empty.", gear_add<0>)); }
    runtime.register_sqfop(binary(4, "addVest", t_object(), t_string(), "Puts on the vest.", gear_add<1>));
    runtime.register_sqfop(binary(4, "addBackpack", t_object(), t_string(), "Puts on the backpack.", gear_add<2>));
    runtime.register_sqfop(binary(4, "addHeadgear", t_object(), t_string(), "Puts on the headgear.", gear_add<3>));
    runtime.register_sqfop(binary(4, "addGoggles", t_object(), t_string(), "Puts on the goggles.", gear_add<4>));
    runtime.register_sqfop(unary("removeUniform", t_object(), "Removes the uniform and its contents.", gear_remove<0>));
    runtime.register_sqfop(unary("removeVest", t_object(), "Removes the vest and its contents.", gear_remove<1>));
    runtime.register_sqfop(unary("removeBackpack", t_object(), "Removes the backpack and its contents.", gear_remove<2>));
    runtime.register_sqfop(unary("removeHeadgear", t_object(), "Removes the headgear.", gear_remove<3>));
    runtime.register_sqfop(unary("removeGoggles", t_object(), "Removes the goggles.", gear_remove<4>));

    runtime.register_sqfop(binary(4, "addWeapon", t_object(), t_string(), "Adds the weapon to its slot and loads a compatible magazine from the containers.", addweapon_object_string));
    runtime.register_sqfop(binary(4, "addWeapon", t_object(), t_array(), "addWeapon [weapon, autoselect]", addweapon_object_array));
    runtime.register_sqfop(binary(4, "addWeaponGlobal", t_object(), t_string(), "Adds the weapon to its slot.", addweapon_object_string));
    runtime.register_sqfop(binary(4, "addWeaponGlobal", t_object(), t_array(), "addWeaponGlobal [weapon, autoselect]", addweapon_object_array));
    runtime.register_sqfop(binary(4, "removeWeapon", t_object(), t_string(), "Removes the weapon.", removeweapon_object_string));
    runtime.register_sqfop(binary(4, "removeWeapon", t_object(), t_array(), "removeWeapon [weapon, autoselect]", removeweapon_object_array));
    runtime.register_sqfop(binary(4, "removeWeaponGlobal", t_object(), t_string(), "Removes the weapon.", removeweapon_object_string));
    runtime.register_sqfop(binary(4, "removeWeaponGlobal", t_object(), t_array(), "removeWeaponGlobal [weapon, autoselect]", removeweapon_object_array));
    runtime.register_sqfop(unary("weapons", t_object(), "Weapons of the unit.", weapons_object));
    runtime.register_sqfop(unary("removeAllWeapons", t_object(), "Removes every weapon and the magazines.", removeallweapons_object));

    runtime.register_sqfop(binary(4, "addMagazine", t_object(), t_any(), "addMagazine magazine, or addMagazine [magazine, ammo].", addmagazine_object_any));
    runtime.register_sqfop(binary(4, "addMagazineGlobal", t_object(), t_string(), "Adds the magazine.", addmagazine_object_any));
    runtime.register_sqfop(binary(4, "addMagazines", t_object(), t_array(), "addMagazines [magazine, count]", addmagazines_object_array));
    runtime.register_sqfop(unary("magazines", t_object(), "Magazines in the containers.", magazines_object));
    runtime.register_sqfop(unary("magazinesAmmo", t_object(), "[[magazine, ammo], ...] of the magazines in the containers.", magazinesammo_object));
    runtime.register_sqfop(binary(4, "removeMagazine", t_object(), t_any(), "Removes one magazine, or [magazine, ammo].", removemagazine_object_any));
    runtime.register_sqfop(binary(4, "removeMagazineGlobal", t_object(), t_string(), "Removes one magazine.", removemagazine_object_any));
    runtime.register_sqfop(binary(4, "removeMagazines", t_object(), t_string(), "Removes every magazine of the class.", removemagazines_object_string));

    runtime.register_sqfop(binary(4, "addItem", t_object(), t_string(), "Puts the item into the uniform, vest or backpack.", additem_object_string));
    runtime.register_sqfop(binary(4, "removeItem", t_object(), t_string(), "Removes one of the item from the containers.", removeitem_object_string));
    runtime.register_sqfop(unary("items", t_object(), "Items in the containers, without magazines.", items_object));
    runtime.register_sqfop(unary("removeAllItems", t_object(), "Removes the items from the containers.", removeallitems_object));
    runtime.register_sqfop(binary(4, "assignItem", t_object(), t_string(), "Moves the item from a container into its slot.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return assign_item(runtime, left, right, true); }));
    runtime.register_sqfop(binary(4, "linkItem", t_object(), t_string(), "Puts the item into its slot.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return assign_item(runtime, left, right, false); }));
    runtime.register_sqfop(binary(4, "unassignItem", t_object(), t_string(), "Moves the item from its slot into a container.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return unassign_item(runtime, left, right, true); }));
    runtime.register_sqfop(binary(4, "unlinkItem", t_object(), t_string(), "Removes the item from its slot.",
        [](::sqf::runtime::runtime& runtime, value::cref left, value::cref right) -> value { return unassign_item(runtime, left, right, false); }));
    runtime.register_sqfop(unary("assignedItems", t_object(), "Items in the assigned slots, and the binocular.", assigneditems_object));
    runtime.register_sqfop(unary("removeAllAssignedItems", t_object(), "Empties the assigned item slots.", removeallassigneditems_object));

    runtime.register_sqfop(binary(4, "addItemToUniform", t_object(), t_string(), "Adds the item or magazine to the uniform.", add_item_to_container<0>));
    runtime.register_sqfop(binary(4, "addItemToVest", t_object(), t_string(), "Adds the item or magazine to the vest.", add_item_to_container<1>));
    runtime.register_sqfop(binary(4, "addItemToBackpack", t_object(), t_string(), "Adds the item or magazine to the backpack.", add_item_to_container<2>));
    runtime.register_sqfop(binary(4, "removeItemFromUniform", t_object(), t_string(), "Removes one of the item from the uniform.", remove_item_from_container<0>));
    runtime.register_sqfop(binary(4, "removeItemFromVest", t_object(), t_string(), "Removes one of the item from the vest.", remove_item_from_container<1>));
    runtime.register_sqfop(binary(4, "removeItemFromBackpack", t_object(), t_string(), "Removes one of the item from the backpack.", remove_item_from_container<2>));
    runtime.register_sqfop(unary("uniformItems", t_object(), "Items and magazines in the uniform.", container_contents<0, false>));
    runtime.register_sqfop(unary("vestItems", t_object(), "Items and magazines in the vest.", container_contents<1, false>));
    runtime.register_sqfop(unary("backpackItems", t_object(), "Items and magazines in the backpack.", container_contents<2, false>));
    runtime.register_sqfop(unary("uniformMagazines", t_object(), "Magazines in the uniform.", container_contents<0, true>));
    runtime.register_sqfop(unary("vestMagazines", t_object(), "Magazines in the vest.", container_contents<1, true>));
    runtime.register_sqfop(unary("backpackMagazines", t_object(), "Magazines in the backpack.", container_contents<2, true>));
    runtime.register_sqfop(unary("clearAllItemsFromBackpack", t_object(), "Empties the backpack.", clear_container<2>));

    const char* cargo_names[][4] = {
        { "addItemCargo", "getItemCargo", "itemCargo", "clearItemCargo" },
        { "addWeaponCargo", "getWeaponCargo", "weaponCargo", "clearWeaponCargo" },
        { "addMagazineCargo", "getMagazineCargo", "magazineCargo", "clearMagazineCargo" },
        { "addBackpackCargo", "getBackpackCargo", "backpackCargo", "clearBackpackCargo" },
    };
    sqfop_binary::callback adders[] = { add_cargo<0>, add_cargo<1>, add_cargo<2>, add_cargo<3> };
    sqfop_unary::callback getters[] = { get_cargo<0>, get_cargo<1>, get_cargo<2>, get_cargo<3> };
    sqfop_unary::callback listers[] = { list_cargo<0>, list_cargo<1>, list_cargo<2>, list_cargo<3> };
    sqfop_unary::callback clearers[] = { clear_cargo<0>, clear_cargo<1>, clear_cargo<2>, clear_cargo<3> };
    for (int kind = 0; kind < 4; kind++)
    {
        auto add = std::string(cargo_names[kind][0]);
        auto clear = std::string(cargo_names[kind][3]);
        runtime.register_sqfop(binary(4, add, t_object(), t_array(), "[class, count]: adds to the cargo.", adders[kind]));
        runtime.register_sqfop(binary(4, add + "Global", t_object(), t_array(), "[class, count]: adds to the cargo.", adders[kind]));
        runtime.register_sqfop(unary(cargo_names[kind][1], t_object(), "[[classes], [counts]] of the cargo.", getters[kind]));
        runtime.register_sqfop(unary(cargo_names[kind][2], t_object(), "One class name per item in the cargo.", listers[kind]));
        runtime.register_sqfop(unary(clear, t_object(), "Empties this part of the cargo.", clearers[kind]));
        runtime.register_sqfop(unary(clear + "Global", t_object(), "Empties this part of the cargo.", clearers[kind]));
    }
}
