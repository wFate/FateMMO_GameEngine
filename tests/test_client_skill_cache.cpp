#include <doctest/doctest.h>
#include "game/shared/client_skill_cache.h"

using namespace fate;

// Helper to create test ClientSkillDef
static ClientSkillDef makeClientDef(const std::string& id,
                                     const std::string& name,
                                     int levelReq,
                                     bool isPassive = false) {
    ClientSkillDef def;
    def.skillId = id;
    def.skillName = name;
    def.description = name + " description";
    def.skillType = isPassive ? "Passive" : "Active";
    def.resourceType = "Mana";
    def.targetType = "SingleEnemy";
    def.levelRequired = levelReq;
    def.range = 3.0f;
    def.isPassive = isPassive;
    def.ranks[0].resourceCost = 5;
    def.ranks[0].cooldownSeconds = 2.0f;
    def.ranks[0].damagePercent = 120;
    def.ranks[1].resourceCost = 8;
    def.ranks[1].cooldownSeconds = 1.8f;
    def.ranks[1].damagePercent = 150;
    def.ranks[2].resourceCost = 12;
    def.ranks[2].cooldownSeconds = 1.5f;
    def.ranks[2].damagePercent = 200;
    return def;
}

TEST_CASE("ClientSkillDefinitionCache: populate and getSkill") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("slash", "Slash", 1));
    skills.push_back(makeClientDef("charge", "Charge", 5));
    skills.push_back(makeClientDef("whirlwind", "Whirlwind", 10));

    ClientSkillDefinitionCache::populate("Warrior", skills);

    const ClientSkillDef* slash = ClientSkillDefinitionCache::getSkill("slash");
    CHECK(slash != nullptr);
    CHECK(slash->skillName == "Slash");
    CHECK(slash->levelRequired == 1);
    CHECK(slash->ranks[0].damagePercent == 120);
    CHECK(slash->ranks[2].damagePercent == 200);

    const ClientSkillDef* charge = ClientSkillDefinitionCache::getSkill("charge");
    CHECK(charge != nullptr);
    CHECK(charge->skillName == "Charge");

    CHECK(ClientSkillDefinitionCache::getSkill("nonexistent") == nullptr);

    ClientSkillDefinitionCache::clear();
}

TEST_CASE("ClientSkillDefinitionCache: getAllSkills sorted by levelRequired") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("whirlwind", "Whirlwind", 10));
    skills.push_back(makeClientDef("slash", "Slash", 1));
    skills.push_back(makeClientDef("charge", "Charge", 5));

    ClientSkillDefinitionCache::populate("Warrior", skills);

    const auto& sorted = ClientSkillDefinitionCache::getAllSkills();
    CHECK(sorted.size() == 3);
    CHECK(sorted[0].skillId == "slash");
    CHECK(sorted[0].levelRequired == 1);
    CHECK(sorted[1].skillId == "charge");
    CHECK(sorted[1].levelRequired == 5);
    CHECK(sorted[2].skillId == "whirlwind");
    CHECK(sorted[2].levelRequired == 10);

    ClientSkillDefinitionCache::clear();
}

TEST_CASE("ClientSkillDefinitionCache: hasSkill") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("slash", "Slash", 1));

    ClientSkillDefinitionCache::populate("Warrior", skills);

    CHECK(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK_FALSE(ClientSkillDefinitionCache::hasSkill("fireball"));

    ClientSkillDefinitionCache::clear();
}

TEST_CASE("ClientSkillDefinitionCache: clear removes all data") {
    std::vector<ClientSkillDef> skills;
    skills.push_back(makeClientDef("slash", "Slash", 1));
    skills.push_back(makeClientDef("charge", "Charge", 5));

    ClientSkillDefinitionCache::populate("Warrior", skills);
    CHECK(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().size() == 2);

    ClientSkillDefinitionCache::clear();
    CHECK_FALSE(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().empty());
}

TEST_CASE("ClientSkillDefinitionCache: populate replaces existing data") {
    std::vector<ClientSkillDef> skills1;
    skills1.push_back(makeClientDef("slash", "Slash", 1));

    ClientSkillDefinitionCache::populate("Warrior", skills1);
    CHECK(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().size() == 1);

    std::vector<ClientSkillDef> skills2;
    skills2.push_back(makeClientDef("fireball", "Fireball", 1));
    skills2.push_back(makeClientDef("frostbolt", "Frostbolt", 5));

    ClientSkillDefinitionCache::populate("Mage", skills2);
    CHECK_FALSE(ClientSkillDefinitionCache::hasSkill("slash"));
    CHECK(ClientSkillDefinitionCache::hasSkill("fireball"));
    CHECK(ClientSkillDefinitionCache::hasSkill("frostbolt"));
    CHECK(ClientSkillDefinitionCache::getAllSkills().size() == 2);

    ClientSkillDefinitionCache::clear();
}
