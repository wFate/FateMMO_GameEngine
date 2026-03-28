#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include "server/cache/collection_cache.h"

namespace fate {

struct CollectionBonuses {
    int bonusSTR = 0, bonusINT = 0, bonusDEX = 0, bonusCON = 0, bonusWIS = 0;
    int bonusMaxHP = 0, bonusMaxMP = 0, bonusDamage = 0, bonusArmor = 0;
    float bonusCritRate = 0.0f, bonusMoveSpeed = 0.0f;

    void addReward(const std::string& type, float value) {
        if      (type == "STR")       bonusSTR += static_cast<int>(value);
        else if (type == "INT")       bonusINT += static_cast<int>(value);
        else if (type == "DEX")       bonusDEX += static_cast<int>(value);
        else if (type == "CON")       bonusCON += static_cast<int>(value);
        else if (type == "WIS")       bonusWIS += static_cast<int>(value);
        else if (type == "MaxHP")     bonusMaxHP += static_cast<int>(value);
        else if (type == "MaxMP")     bonusMaxMP += static_cast<int>(value);
        else if (type == "Damage")    bonusDamage += static_cast<int>(value);
        else if (type == "Armor")     bonusArmor += static_cast<int>(value);
        else if (type == "CritRate")  bonusCritRate += value;
        else if (type == "MoveSpeed") bonusMoveSpeed += value;
    }

    void reset() { *this = CollectionBonuses{}; }
};

class CollectionState {
public:
    std::unordered_set<uint32_t> completedIds;
    CollectionBonuses bonuses;

    bool isCompleted(uint32_t id) const { return completedIds.count(id) > 0; }
    void markCompleted(uint32_t id) { completedIds.insert(id); }
    int completedCount() const { return static_cast<int>(completedIds.size()); }

    void recalculateBonuses(const std::unordered_map<uint32_t, CachedCollection>& allDefs) {
        bonuses.reset();
        for (uint32_t id : completedIds) {
            auto it = allDefs.find(id);
            if (it != allDefs.end()) {
                bonuses.addReward(it->second.rewardType, it->second.rewardValue);
            }
        }
    }
};

struct PlayerCollectionState {
    int level = 0;
    int totalMobKills = 0;
    int arenaWins = 0;
    int battlefieldWins = 0;
    int dungeonCompletions = 0;
    int learnedSkills = 0;
    bool inGuild = false;
    int maxEnchantLevel = 0;
    int uncommonItems = 0, rareItems = 0, epicItems = 0, legendaryItems = 0;
    float totalPlaytimeHours = 0.0f;
    std::unordered_set<std::string> ownedItemIds;
};

inline bool evaluateCollectionCondition(const CachedCollection& def, const PlayerCollectionState& state) {
    if (def.conditionType == "ReachLevel")        return state.level >= def.conditionValue;
    if (def.conditionType == "TotalMobKills")     return state.totalMobKills >= def.conditionValue;
    if (def.conditionType == "KillBoss")          return state.totalMobKills > 0; // any boss kill
    if (def.conditionType == "WinArena")          return state.arenaWins >= def.conditionValue;
    if (def.conditionType == "WinBattlefield")    return state.battlefieldWins >= def.conditionValue;
    if (def.conditionType == "CompleteDungeon")    return state.dungeonCompletions >= def.conditionValue;
    if (def.conditionType == "JoinGuild")         return state.inGuild;
    if (def.conditionType == "LearnSkills")        return state.learnedSkills >= def.conditionValue;
    if (def.conditionType == "ReachEnchant")      return state.maxEnchantLevel >= def.conditionValue;
    if (def.conditionType == "OwnItemRarity") {
        if (def.conditionTarget == "Uncommon")  return state.uncommonItems >= def.conditionValue;
        if (def.conditionTarget == "Rare")      return state.rareItems >= def.conditionValue;
        if (def.conditionTarget == "Epic")      return state.epicItems >= def.conditionValue;
        if (def.conditionTarget == "Legendary") return state.legendaryItems >= def.conditionValue;
    }
    if (def.conditionType == "OwnItem") {
        return state.ownedItemIds.count(def.conditionTarget) > 0;
    }
    return false;
}

} // namespace fate
