-- Phase 7 base schema for fate_engine_dev
-- Tables: accounts, characters, item_definitions, character_inventory
-- Schema matches Unity project's fate_mmo database exactly

-- ============================================================
-- ACCOUNTS
-- ============================================================
CREATE TABLE IF NOT EXISTS accounts (
    account_id SERIAL PRIMARY KEY,
    username VARCHAR(64) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    email VARCHAR(128) UNIQUE NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    last_login TIMESTAMPTZ,
    is_active BOOLEAN DEFAULT TRUE,
    is_banned BOOLEAN DEFAULT FALSE,
    ban_reason TEXT,
    ban_expires_at TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_accounts_username ON accounts(username);
CREATE INDEX IF NOT EXISTS idx_accounts_email ON accounts(email);

-- ============================================================
-- CHARACTERS
-- ============================================================
CREATE TABLE IF NOT EXISTS characters (
    character_id VARCHAR(64) PRIMARY KEY,
    account_id INTEGER NOT NULL REFERENCES accounts(account_id) ON DELETE CASCADE,
    character_name VARCHAR(64) NOT NULL,
    class_name VARCHAR(32) DEFAULT 'Warrior',
    level INTEGER DEFAULT 1,
    current_xp BIGINT DEFAULT 0,
    xp_to_next_level INTEGER DEFAULT 100,
    current_scene VARCHAR(64) DEFAULT 'Scene2',
    position_x REAL DEFAULT 0,
    position_y REAL DEFAULT 0,
    current_hp INTEGER DEFAULT 100,
    max_hp INTEGER DEFAULT 100,
    current_mp INTEGER DEFAULT 50,
    max_mp INTEGER DEFAULT 50,
    base_strength INTEGER DEFAULT 10,
    base_vitality INTEGER DEFAULT 10,
    base_intelligence INTEGER DEFAULT 10,
    base_dexterity INTEGER DEFAULT 10,
    base_wisdom INTEGER DEFAULT 10,
    gold BIGINT DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    last_login TIMESTAMPTZ,
    last_saved_at TIMESTAMPTZ DEFAULT NOW(),
    total_playtime_seconds BIGINT DEFAULT 0,
    gender INTEGER DEFAULT 0,
    hairstyle INTEGER DEFAULT 0,
    hair_color INTEGER DEFAULT 0,
    merchant_pass_expires_at TIMESTAMPTZ,
    guild_id INTEGER,
    honor INTEGER DEFAULT 0,
    pvp_kills INTEGER DEFAULT 0,
    pvp_deaths INTEGER DEFAULT 0,
    last_online TIMESTAMPTZ,
    guild_left_at TIMESTAMPTZ,
    current_fury REAL DEFAULT 0,
    is_dead BOOLEAN NOT NULL DEFAULT FALSE,
    death_timestamp BIGINT
);

CREATE INDEX IF NOT EXISTS idx_characters_account ON characters(account_id);
CREATE INDEX IF NOT EXISTS idx_characters_scene ON characters(current_scene);
CREATE INDEX IF NOT EXISTS idx_characters_name ON characters(character_name);
CREATE UNIQUE INDEX IF NOT EXISTS characters_character_name_uq ON characters(character_name);
CREATE UNIQUE INDEX IF NOT EXISTS characters_character_name_lower_uq ON characters(lower(character_name));

-- ============================================================
-- ITEM DEFINITIONS (needed for character_inventory FK)
-- ============================================================
CREATE TABLE IF NOT EXISTS item_definitions (
    item_id VARCHAR(64) PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    type VARCHAR(32) NOT NULL,
    subtype VARCHAR(32),
    class_req VARCHAR(32) DEFAULT 'All',
    level_req INTEGER DEFAULT 1,
    damage_min INTEGER DEFAULT 0,
    damage_max INTEGER DEFAULT 0,
    armor INTEGER DEFAULT 0,
    attributes JSONB DEFAULT '{}',
    description TEXT,
    gold_value INTEGER DEFAULT 0,
    max_stack INTEGER DEFAULT 1,
    icon_path VARCHAR(255),
    possible_stats JSONB DEFAULT '[]',
    is_socketable BOOLEAN DEFAULT FALSE,
    is_soulbound BOOLEAN DEFAULT FALSE,
    rarity VARCHAR(32) DEFAULT 'Common',
    required_level INTEGER,
    max_enchant INTEGER DEFAULT 12
);

-- ============================================================
-- CHARACTER INVENTORY
-- ============================================================
CREATE TABLE IF NOT EXISTS character_inventory (
    instance_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    character_id VARCHAR(64) NOT NULL REFERENCES characters(character_id) ON DELETE CASCADE,
    item_id VARCHAR(64) NOT NULL REFERENCES item_definitions(item_id),
    slot_index INTEGER,
    bag_slot_index INTEGER,
    bag_item_slot INTEGER,
    rolled_stats JSONB NOT NULL DEFAULT '{}',
    socket_stat VARCHAR(16),
    socket_value INTEGER,
    enchant_level INTEGER DEFAULT 0,
    is_protected BOOLEAN DEFAULT FALSE,
    is_soulbound BOOLEAN DEFAULT FALSE,
    is_equipped BOOLEAN DEFAULT FALSE,
    equipped_slot VARCHAR(32),
    quantity INTEGER DEFAULT 1,
    acquired_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_char_inv_character ON character_inventory(character_id);
CREATE INDEX IF NOT EXISTS idx_char_inv_slot ON character_inventory(character_id, slot_index) WHERE slot_index IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_char_inv_equipped ON character_inventory(character_id, is_equipped) WHERE is_equipped = true;
