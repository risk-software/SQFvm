#pragma once
#include <array>
#include <string>
#include <vector>

namespace sqf
{
    namespace runtime
    {
        class runtime;
    }
    namespace types
    {
        /// One weapon slot as in getUnitLoadout: weapon, attachments and loaded magazines.
        struct weapon_slot
        {
            std::string weapon;
            std::string muzzle;
            std::string pointer;
            std::string optic;
            std::string bipod;
            std::string magazine;
            int magazine_ammo = 0;
            std::string secondary_magazine;
            int secondary_magazine_ammo = 0;
            bool empty() const { return weapon.empty(); }
        };

        /// An entry in a container. Magazines carry ammo; other items have ammo -1.
        struct container_entry
        {
            std::string class_name;
            int count;
            int ammo;
        };

        struct item_container
        {
            std::string class_name;
            std::vector<container_entry> contents;
            bool empty() const { return class_name.empty(); }
        };

        /// Inventory of a unit, or the cargo of a vehicle (which only uses `cargo`).
        struct unit_inventory
        {
            weapon_slot primary;
            weapon_slot secondary;
            weapon_slot handgun;
            weapon_slot binocular;
            item_container uniform;
            item_container vest;
            item_container backpack;
            std::string headgear;
            std::string goggles;
            // Map, GPS or terminal, radio, compass, watch, night vision.
            std::array<std::string, 6> assigned;
            // Vehicle and crate cargo: items, weapons, magazines and backpacks.
            std::vector<container_entry> cargo_items;
            std::vector<container_entry> cargo_weapons;
            std::vector<container_entry> cargo_magazines;
            std::vector<container_entry> cargo_backpacks;
        };
    }

    namespace operators
    {
        /// Unit loadouts (getUnitLoadout, setUnitLoadout), weapons, magazines, items, containers and
        /// vehicle cargo.
        void ops_inventory(::sqf::runtime::runtime& runtime);
    }
}
