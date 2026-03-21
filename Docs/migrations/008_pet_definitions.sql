-- Migration 008: Pet definitions table
-- Run after 007_enchant_and_crafting.sql
-- Allows adding pet types via DB instead of hardcoding in server

CREATE TABLE IF NOT EXISTS pet_definitions (
    pet_id VARCHAR(64) PRIMARY KEY,
    display_name VARCHAR(64) NOT NULL,
    rarity VARCHAR(32) DEFAULT 'Common',
    base_hp INTEGER DEFAULT 10,
    base_crit_rate REAL DEFAULT 0.01,
    base_exp_bonus REAL DEFAULT 0.0,
    hp_per_level REAL DEFAULT 2.0,
    crit_per_level REAL DEFAULT 0.002,
    exp_bonus_per_level REAL DEFAULT 0.0,
    description TEXT,
    icon_path VARCHAR(255)
);

-- Seed starter pets
INSERT INTO pet_definitions (pet_id, display_name, rarity, base_hp, base_crit_rate, base_exp_bonus, hp_per_level, crit_per_level, exp_bonus_per_level, description)
VALUES
    ('pet_wolf', 'Wolf', 'Common', 10, 0.01, 0.0, 2.0, 0.002, 0.0, 'A loyal wolf companion. Balanced HP and crit.'),
    ('pet_hawk', 'Hawk', 'Uncommon', 5, 0.02, 0.05, 1.0, 0.003, 0.005, 'A keen-eyed hawk. High crit rate and XP bonus.'),
    ('pet_turtle', 'Turtle', 'Common', 20, 0.0, 0.0, 4.0, 0.0, 0.0, 'A sturdy turtle. Maximum HP bonus, no crit.')
ON CONFLICT (pet_id) DO NOTHING;
