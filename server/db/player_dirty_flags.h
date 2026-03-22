#pragma once
#include <cstdint>

namespace fate {

struct PlayerDirtyFlags {
    bool position = false;
    bool vitals = false;      // HP/MP
    bool stats = false;       // level, XP, base stats
    bool inventory = false;
    bool skills = false;
    bool quests = false;
    bool bank = false;
    bool pet = false;
    bool social = false;      // friends, blocks
    bool guild = false;

    bool any() const {
        return position || vitals || stats || inventory || skills ||
               quests || bank || pet || social || guild;
    }

    void clearAll() {
        position = vitals = stats = inventory = skills =
            quests = bank = pet = social = guild = false;
    }
};

} // namespace fate
