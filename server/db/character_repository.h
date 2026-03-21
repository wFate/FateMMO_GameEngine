#pragma once
#include <string>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>

namespace fate {

struct CharacterRecord {
    std::string character_id;
    int account_id = 0;
    std::string character_name;
    std::string class_name = "Warrior";

    // Progression
    int level = 1;
    int64_t current_xp = 0;
    int xp_to_next_level = 100;

    // Position
    std::string current_scene = "WhisperingWoods";
    float position_x = 0.0f;
    float position_y = 0.0f;

    // Vitals
    int current_hp = 100;
    int max_hp = 100;
    int current_mp = 50;
    int max_mp = 50;
    float current_fury = 0.0f;

    // Base stats
    int base_strength = 10;
    int base_vitality = 10;
    int base_intelligence = 10;
    int base_dexterity = 10;
    int base_wisdom = 10;

    // Economy
    int64_t gold = 0;

    // PvP
    int honor = 0;
    int pvp_kills = 0;
    int pvp_deaths = 0;

    // Death
    bool is_dead = false;
    int64_t death_timestamp = 0;

    // Appearance
    int gender = 0;
    int hairstyle = 0;
    int hair_color = 0;

    // Playtime
    int64_t total_playtime_seconds = 0;
};

class CharacterRepository {
public:
    explicit CharacterRepository(pqxx::connection& conn) : conn_(conn) {}

    // Create a new character with defaults for the given account.
    // character_id is generated as "chr_<accountId>_<timestamp>".
    // Returns the character_id on success, empty string on failure.
    std::string createDefaultCharacter(int accountId, const std::string& characterName,
                                       const std::string& className);

    // Load full character data. Returns nullopt if not found.
    std::optional<CharacterRecord> loadCharacter(const std::string& characterId);

    // Load character by account_id (for one-char-per-account model).
    std::optional<CharacterRecord> loadCharacterByAccount(int accountId);

    // Save character state (vitals, position, stats, gold, pvp, death).
    // Called on disconnect. Returns true on success.
    bool saveCharacter(const CharacterRecord& rec);

private:
    pqxx::connection& conn_;

    // Map a result row to a CharacterRecord.
    static CharacterRecord rowToRecord(const pqxx::row& row);
};

} // namespace fate
