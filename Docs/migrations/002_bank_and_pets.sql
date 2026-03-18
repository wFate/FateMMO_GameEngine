-- Migration 002: Bank Storage and Pet tables
-- Applied: March 18, 2026
-- Database: fate_engine_dev

CREATE TABLE IF NOT EXISTS character_bank (
    id SERIAL PRIMARY KEY,
    character_id VARCHAR(64) NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    slot_index INTEGER NOT NULL,
    item_id VARCHAR(64) NOT NULL REFERENCES item_definitions(item_id),
    quantity INTEGER DEFAULT 1,
    rolled_stats JSONB DEFAULT '{}',
    socket_stat VARCHAR(16),
    socket_value INTEGER,
    enchant_level INTEGER DEFAULT 0,
    is_protected BOOLEAN DEFAULT FALSE,
    instance_id UUID DEFAULT gen_random_uuid(),
    UNIQUE(character_id, slot_index)
);

CREATE TABLE IF NOT EXISTS character_bank_gold (
    character_id VARCHAR(64) PRIMARY KEY REFERENCES characters(character_id) ON DELETE CASCADE,
    stored_gold BIGINT DEFAULT 0
);

CREATE TABLE IF NOT EXISTS character_pets (
    id SERIAL PRIMARY KEY,
    character_id VARCHAR(64) NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    pet_def_id VARCHAR(64) NOT NULL,
    pet_name VARCHAR(64) NOT NULL,
    level INTEGER DEFAULT 1,
    current_xp BIGINT DEFAULT 0,
    is_equipped BOOLEAN DEFAULT FALSE,
    is_soulbound BOOLEAN DEFAULT TRUE,
    auto_loot_enabled BOOLEAN DEFAULT FALSE,
    acquired_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX idx_character_bank_char ON character_bank(character_id);
CREATE INDEX idx_character_pets_char ON character_pets(character_id);
