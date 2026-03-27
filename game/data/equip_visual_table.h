#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>

namespace fate {

inline const std::unordered_map<uint16_t, std::string>& weaponVisualPaths() {
    static const std::unordered_map<uint16_t, std::string> table = {
        {1, "assets/sprites/equipment/weapon_rusty_dagger.png"},
        {2, "assets/sprites/equipment/weapon_iron_sword.png"},
        {3, "assets/sprites/equipment/weapon_oak_staff.png"},
        {4, "assets/sprites/equipment/weapon_short_bow.png"},
    };
    return table;
}

inline const std::unordered_map<uint16_t, std::string>& armorVisualPaths() {
    static const std::unordered_map<uint16_t, std::string> table = {
        {1, "assets/sprites/equipment/armor_quilted_vest.png"},
        {2, "assets/sprites/equipment/armor_iron_plate.png"},
        {3, "assets/sprites/equipment/armor_cloth_robe.png"},
    };
    return table;
}

inline const std::unordered_map<uint16_t, std::string>& hatVisualPaths() {
    static const std::unordered_map<uint16_t, std::string> table = {
        {1, "assets/sprites/equipment/hat_starter_cap.png"},
        {2, "assets/sprites/equipment/hat_iron_helm.png"},
        {3, "assets/sprites/equipment/hat_wizard_hat.png"},
    };
    return table;
}

inline const std::string& getWeaponSpritePath(uint16_t idx) {
    static const std::string empty;
    auto& t = weaponVisualPaths();
    auto it = t.find(idx);
    return it != t.end() ? it->second : empty;
}

inline const std::string& getArmorSpritePath(uint16_t idx) {
    static const std::string empty;
    auto& t = armorVisualPaths();
    auto it = t.find(idx);
    return it != t.end() ? it->second : empty;
}

inline const std::string& getHatSpritePath(uint16_t idx) {
    static const std::string empty;
    auto& t = hatVisualPaths();
    auto it = t.find(idx);
    return it != t.end() ? it->second : empty;
}

} // namespace fate
