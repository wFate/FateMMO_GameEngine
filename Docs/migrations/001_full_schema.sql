-- =============================================================================
-- Migration: 001_full_schema.sql
-- Target database: fate_engine_dev
-- Generated: 2026-03-18
-- Description: Full schema recreation for the fate_mmo database (65 tables).
--              Includes all tables in FK-dependency order, primary keys,
--              foreign key constraints, and non-PK indexes.
-- =============================================================================

-- Enable pgcrypto for gen_random_uuid() if not already enabled
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- Table: accounts
CREATE TABLE IF NOT EXISTS accounts (
    account_id SERIAL NOT NULL,
    username VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    email VARCHAR(128) NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now(),
    last_login TIMESTAMPTZ,
    is_active BOOLEAN DEFAULT true,
    is_banned BOOLEAN DEFAULT false,
    ban_reason TEXT,
    ban_expires_at TIMESTAMPTZ,
    CONSTRAINT accounts_pkey PRIMARY KEY (account_id)
);

-- Table: bag_definitions
CREATE TABLE IF NOT EXISTS bag_definitions (
    bag_id VARCHAR(64) NOT NULL,
    name VARCHAR(100) NOT NULL,
    slot_count INTEGER NOT NULL,
    rarity VARCHAR(32) DEFAULT 'Common'::character varying,
    required_level INTEGER DEFAULT 1,
    description TEXT,
    gold_value INTEGER DEFAULT 0,
    icon_path VARCHAR(255),
    CONSTRAINT bag_definitions_pkey PRIMARY KEY (bag_id)
);

-- Table: characters
CREATE TABLE IF NOT EXISTS characters (
    character_id VARCHAR(64) NOT NULL,
    account_id INTEGER NOT NULL,
    character_name VARCHAR(64) NOT NULL,
    class_name VARCHAR(32) DEFAULT 'Warrior'::character varying,
    level INTEGER DEFAULT 1,
    current_xp BIGINT DEFAULT 0,
    xp_to_next_level INTEGER DEFAULT 100,
    current_scene VARCHAR(64) DEFAULT 'Scene2'::character varying,
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
    created_at TIMESTAMPTZ DEFAULT now(),
    last_login TIMESTAMPTZ,
    last_saved_at TIMESTAMPTZ DEFAULT now(),
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
    is_dead BOOLEAN DEFAULT false NOT NULL,
    death_timestamp BIGINT,
    CONSTRAINT characters_pkey PRIMARY KEY (character_id)
);

-- Table: blocked_players
CREATE TABLE IF NOT EXISTS blocked_players (
    blocker_character_id VARCHAR(64) NOT NULL,
    blocked_character_id VARCHAR(64) NOT NULL,
    blocked_at TIMESTAMPTZ DEFAULT now(),
    reason VARCHAR(256),
    CONSTRAINT blocked_players_pkey PRIMARY KEY (blocker_character_id, blocked_character_id)
);

-- Table: bounties
CREATE TABLE IF NOT EXISTS bounties (
    bounty_id SERIAL NOT NULL,
    target_character_id VARCHAR(64) NOT NULL,
    target_character_name VARCHAR(64) NOT NULL,
    total_amount BIGINT DEFAULT 0 NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now() NOT NULL,
    expires_at TIMESTAMPTZ DEFAULT (now() + '2 days'::interval) NOT NULL,
    is_active BOOLEAN DEFAULT true,
    CONSTRAINT bounties_pkey PRIMARY KEY (bounty_id)
);

-- Table: bounty_claim_log
CREATE TABLE IF NOT EXISTS bounty_claim_log (
    claim_id SERIAL NOT NULL,
    claimer_character_id VARCHAR(64) NOT NULL,
    target_character_id VARCHAR(64) NOT NULL,
    amount_claimed BIGINT NOT NULL,
    claimed_at TIMESTAMPTZ DEFAULT now() NOT NULL,
    CONSTRAINT bounty_claim_log_pkey PRIMARY KEY (claim_id)
);

-- Table: bounty_constants
CREATE TABLE IF NOT EXISTS bounty_constants (
    key VARCHAR(64) NOT NULL,
    value BIGINT NOT NULL,
    description TEXT,
    CONSTRAINT bounty_constants_pkey PRIMARY KEY (key)
);

-- Table: bounty_contributions
CREATE TABLE IF NOT EXISTS bounty_contributions (
    contribution_id SERIAL NOT NULL,
    bounty_id INTEGER NOT NULL,
    contributor_character_id VARCHAR(64) NOT NULL,
    contributor_character_name VARCHAR(64) NOT NULL,
    amount BIGINT NOT NULL,
    contributed_at TIMESTAMPTZ DEFAULT now() NOT NULL,
    is_cancelled BOOLEAN DEFAULT false,
    cancelled_at TIMESTAMPTZ,
    CONSTRAINT bounty_contributions_pkey PRIMARY KEY (contribution_id)
);

-- Table: bounty_history
CREATE TABLE IF NOT EXISTS bounty_history (
    history_id SERIAL NOT NULL,
    event_type VARCHAR(32) NOT NULL,
    target_character_id VARCHAR(64) NOT NULL,
    target_character_name VARCHAR(64) NOT NULL,
    actor_character_id VARCHAR(64),
    actor_character_name VARCHAR(64),
    amount BIGINT NOT NULL,
    tax_amount BIGINT DEFAULT 0 NOT NULL,
    party_size INTEGER DEFAULT 1,
    amount_per_member BIGINT,
    occurred_at TIMESTAMPTZ DEFAULT now() NOT NULL,
    metadata JSONB DEFAULT '{}'::jsonb,
    CONSTRAINT bounty_history_pkey PRIMARY KEY (history_id)
);

-- Table: cached_class_rankings
CREATE TABLE IF NOT EXISTS cached_class_rankings (
    character_id VARCHAR(64) NOT NULL,
    character_name VARCHAR(64) NOT NULL,
    class_name VARCHAR(32) NOT NULL,
    level INTEGER NOT NULL,
    current_xp BIGINT NOT NULL,
    guild_id INTEGER,
    guild_name VARCHAR(24),
    rank_position INTEGER NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT cached_class_rankings_pkey PRIMARY KEY (class_name, character_id)
);

-- Table: cached_guild_rankings
CREATE TABLE IF NOT EXISTS cached_guild_rankings (
    guild_id INTEGER NOT NULL,
    guild_name VARCHAR(24) NOT NULL,
    guild_level INTEGER NOT NULL,
    guild_xp BIGINT NOT NULL,
    member_count INTEGER NOT NULL,
    owner_name VARCHAR(64) NOT NULL,
    rank_position INTEGER NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT cached_guild_rankings_pkey PRIMARY KEY (guild_id)
);

-- Table: cached_player_rankings
CREATE TABLE IF NOT EXISTS cached_player_rankings (
    character_id VARCHAR(64) NOT NULL,
    character_name VARCHAR(64) NOT NULL,
    class_name VARCHAR(32) NOT NULL,
    level INTEGER NOT NULL,
    current_xp BIGINT NOT NULL,
    guild_id INTEGER,
    guild_name VARCHAR(24),
    rank_position INTEGER NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT cached_player_rankings_pkey PRIMARY KEY (character_id)
);

-- Table: character_bags
CREATE TABLE IF NOT EXISTS character_bags (
    id SERIAL NOT NULL,
    character_id VARCHAR(64) NOT NULL,
    slot_index INTEGER NOT NULL,
    bag_id VARCHAR(64) NOT NULL,
    CONSTRAINT character_bags_pkey PRIMARY KEY (id)
);

-- Table: item_definitions
CREATE TABLE IF NOT EXISTS item_definitions (
    item_id VARCHAR(64) NOT NULL,
    name VARCHAR(100) NOT NULL,
    type VARCHAR(32) NOT NULL,
    subtype VARCHAR(32),
    class_req VARCHAR(32) DEFAULT 'All'::character varying,
    level_req INTEGER DEFAULT 1,
    damage_min INTEGER DEFAULT 0,
    damage_max INTEGER DEFAULT 0,
    armor INTEGER DEFAULT 0,
    attributes JSONB DEFAULT '{}'::jsonb,
    description TEXT,
    gold_value INTEGER DEFAULT 0,
    max_stack INTEGER DEFAULT 1,
    icon_path VARCHAR(255),
    possible_stats JSONB DEFAULT '[]'::jsonb,
    is_socketable BOOLEAN DEFAULT false,
    is_soulbound BOOLEAN DEFAULT false,
    rarity VARCHAR(32) DEFAULT 'Common'::character varying,
    required_level INTEGER,
    max_enchant INTEGER DEFAULT 12,
    CONSTRAINT item_definitions_pkey PRIMARY KEY (item_id)
);

-- Table: character_inventory
CREATE TABLE IF NOT EXISTS character_inventory (
    instance_id UUID DEFAULT gen_random_uuid() NOT NULL,
    character_id VARCHAR(64) NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    slot_index INTEGER,
    bag_slot_index INTEGER,
    bag_item_slot INTEGER,
    rolled_stats JSONB DEFAULT '{}'::jsonb NOT NULL,
    socket_stat VARCHAR(16),
    socket_value INTEGER,
    enchant_level INTEGER DEFAULT 0,
    is_protected BOOLEAN DEFAULT false,
    is_soulbound BOOLEAN DEFAULT false,
    is_equipped BOOLEAN DEFAULT false,
    equipped_slot VARCHAR(32),
    quantity INTEGER DEFAULT 1,
    acquired_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT valid_inventory_position CHECK ((slot_index IS NOT NULL AND bag_slot_index IS NULL AND bag_item_slot IS NULL) OR (slot_index IS NULL AND bag_slot_index IS NOT NULL AND bag_item_slot IS NOT NULL) OR (slot_index IS NULL AND bag_slot_index IS NULL AND bag_item_slot IS NULL)),
    CONSTRAINT character_inventory_pkey PRIMARY KEY (instance_id)
);

-- Table: character_skill_bar
CREATE TABLE IF NOT EXISTS character_skill_bar (
    character_id VARCHAR(36) NOT NULL,
    slot_index INTEGER NOT NULL,
    skill_id VARCHAR(64),
    CONSTRAINT character_skill_bar_pkey PRIMARY KEY (character_id, slot_index)
);

-- Table: character_skill_points
CREATE TABLE IF NOT EXISTS character_skill_points (
    character_id VARCHAR(64) NOT NULL,
    total_earned INTEGER DEFAULT 0 NOT NULL,
    total_spent INTEGER DEFAULT 0 NOT NULL,
    updated_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT character_skill_points_pkey PRIMARY KEY (character_id)
);

-- Table: skill_definitions
CREATE TABLE IF NOT EXISTS skill_definitions (
    skill_id VARCHAR(64) NOT NULL,
    skill_name VARCHAR(64) NOT NULL,
    class_req VARCHAR(32) NOT NULL,
    skill_type VARCHAR(16) DEFAULT 'Active'::character varying NOT NULL,
    level_required INTEGER DEFAULT 1 NOT NULL,
    resource_type VARCHAR(16) DEFAULT 'Fury'::character varying NOT NULL,
    target_type VARCHAR(32) DEFAULT 'SingleEnemy'::character varying NOT NULL,
    range_tiles DOUBLE PRECISION DEFAULT 1.5,
    aoe_radius DOUBLE PRECISION DEFAULT 0,
    damage_type VARCHAR(16) DEFAULT 'Physical'::character varying,
    can_crit BOOLEAN DEFAULT true,
    uses_hit_rate BOOLEAN DEFAULT true,
    fury_on_hit DOUBLE PRECISION DEFAULT 0,
    is_ultimate BOOLEAN DEFAULT false,
    cast_time DOUBLE PRECISION DEFAULT 0,
    channel_time DOUBLE PRECISION DEFAULT 0,
    applies_bleed BOOLEAN DEFAULT false,
    applies_burn BOOLEAN DEFAULT false,
    applies_poison BOOLEAN DEFAULT false,
    applies_slow BOOLEAN DEFAULT false,
    applies_taunt BOOLEAN DEFAULT false,
    applies_freeze BOOLEAN DEFAULT false,
    grants_invulnerability BOOLEAN DEFAULT false,
    grants_stun_immunity BOOLEAN DEFAULT false,
    grants_crit_guarantee BOOLEAN DEFAULT false,
    removes_debuffs BOOLEAN DEFAULT false,
    teleport_distance DOUBLE PRECISION DEFAULT 0,
    dash_distance DOUBLE PRECISION DEFAULT 0,
    is_resurrection BOOLEAN DEFAULT false,
    locks_movement BOOLEAN DEFAULT false,
    consumes_all_resource BOOLEAN DEFAULT false,
    scales_with_resource BOOLEAN DEFAULT false,
    description TEXT,
    animation_trigger VARCHAR(64),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT skill_definitions_pkey PRIMARY KEY (skill_id)
);

-- Table: character_skills
CREATE TABLE IF NOT EXISTS character_skills (
    character_id VARCHAR(36) NOT NULL,
    skill_id VARCHAR(64) NOT NULL,
    activated_rank INTEGER DEFAULT 1 NOT NULL,
    learned_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    unlocked_rank INTEGER DEFAULT 0 NOT NULL,
    CONSTRAINT character_skills_pkey PRIMARY KEY (character_id, skill_id)
);

-- Table: crafting_recipes
CREATE TABLE IF NOT EXISTS crafting_recipes (
    recipe_id VARCHAR(64) NOT NULL,
    recipe_name VARCHAR(128) NOT NULL,
    recipe_type VARCHAR(32) DEFAULT 'skillbook'::character varying,
    result_item_id VARCHAR(64) NOT NULL,
    result_quantity INTEGER DEFAULT 1,
    level_req INTEGER DEFAULT 1,
    class_req VARCHAR(32),
    gold_cost INTEGER DEFAULT 0,
    crafting_time INTEGER DEFAULT 0,
    description TEXT,
    CONSTRAINT crafting_recipes_pkey PRIMARY KEY (recipe_id)
);

-- Table: crafting_ingredients
CREATE TABLE IF NOT EXISTS crafting_ingredients (
    ingredient_id SERIAL NOT NULL,
    recipe_id VARCHAR(64) NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    quantity INTEGER DEFAULT 1,
    CONSTRAINT crafting_ingredients_pkey PRIMARY KEY (ingredient_id)
);

-- Table: friends
CREATE TABLE IF NOT EXISTS friends (
    character_id VARCHAR(64) NOT NULL,
    friend_character_id VARCHAR(64) NOT NULL,
    status VARCHAR(16) DEFAULT 'pending'::character varying NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now(),
    accepted_at TIMESTAMPTZ,
    note VARCHAR(100) DEFAULT ''::character varying,
    CONSTRAINT friends_pkey PRIMARY KEY (character_id, friend_character_id)
);

-- Table: gauntlet_config
CREATE TABLE IF NOT EXISTS gauntlet_config (
    division_id SERIAL NOT NULL,
    division_name VARCHAR(32) NOT NULL,
    min_level INTEGER NOT NULL,
    max_level INTEGER NOT NULL,
    arena_scene_name VARCHAR(64) NOT NULL,
    wave_count INTEGER DEFAULT 5,
    seconds_between_waves INTEGER DEFAULT 10,
    respawn_seconds INTEGER DEFAULT 10,
    team_spawn_a_x DOUBLE PRECISION DEFAULT 0,
    team_spawn_a_y DOUBLE PRECISION DEFAULT 0,
    team_spawn_b_x DOUBLE PRECISION DEFAULT 0,
    team_spawn_b_y DOUBLE PRECISION DEFAULT 0,
    min_players_to_start INTEGER DEFAULT 2,
    max_players_per_team INTEGER DEFAULT 10,
    CONSTRAINT gauntlet_config_pkey PRIMARY KEY (division_id)
);

-- Table: gauntlet_performance_rewards
CREATE TABLE IF NOT EXISTS gauntlet_performance_rewards (
    reward_id SERIAL NOT NULL,
    division_id INTEGER,
    category VARCHAR(32) NOT NULL,
    reward_type VARCHAR(16) NOT NULL,
    reward_value VARCHAR(64) NOT NULL,
    quantity INTEGER DEFAULT 1,
    CONSTRAINT gauntlet_performance_rewards_pkey PRIMARY KEY (reward_id)
);

-- Table: gauntlet_results
CREATE TABLE IF NOT EXISTS gauntlet_results (
    result_id SERIAL NOT NULL,
    division_id INTEGER NOT NULL,
    match_timestamp TIMESTAMP DEFAULT now(),
    winning_team VARCHAR(1),
    team_a_score INTEGER DEFAULT 0,
    team_b_score INTEGER DEFAULT 0,
    player_count INTEGER DEFAULT 0,
    duration_seconds INTEGER DEFAULT 0,
    CONSTRAINT gauntlet_results_pkey PRIMARY KEY (result_id)
);

-- Table: gauntlet_player_results
CREATE TABLE IF NOT EXISTS gauntlet_player_results (
    id SERIAL NOT NULL,
    result_id INTEGER,
    character_id VARCHAR(64) NOT NULL,
    character_name VARCHAR(64),
    team VARCHAR(1) NOT NULL,
    mob_kills INTEGER DEFAULT 0,
    player_kills INTEGER DEFAULT 0,
    deaths INTEGER DEFAULT 0,
    points_contributed INTEGER DEFAULT 0,
    was_winner BOOLEAN DEFAULT false,
    CONSTRAINT gauntlet_player_results_pkey PRIMARY KEY (id)
);

-- Table: gauntlet_rewards
CREATE TABLE IF NOT EXISTS gauntlet_rewards (
    reward_id SERIAL NOT NULL,
    division_id INTEGER,
    is_winner BOOLEAN NOT NULL,
    reward_type VARCHAR(16) NOT NULL,
    reward_value VARCHAR(64) NOT NULL,
    quantity INTEGER DEFAULT 1,
    CONSTRAINT gauntlet_rewards_pkey PRIMARY KEY (reward_id)
);

-- Table: gauntlet_waves
CREATE TABLE IF NOT EXISTS gauntlet_waves (
    wave_id SERIAL NOT NULL,
    division_id INTEGER,
    wave_number INTEGER NOT NULL,
    mob_def_id VARCHAR(64) NOT NULL,
    mob_count INTEGER DEFAULT 5,
    spawn_delay_seconds DOUBLE PRECISION DEFAULT 0,
    is_boss BOOLEAN DEFAULT false,
    bonus_points INTEGER DEFAULT 0,
    CONSTRAINT gauntlet_waves_pkey PRIMARY KEY (wave_id)
);

-- Table: guilds
CREATE TABLE IF NOT EXISTS guilds (
    guild_id SERIAL NOT NULL,
    guild_name VARCHAR(24) NOT NULL,
    symbol_index SMALLINT DEFAULT 0,
    owner_character_id VARCHAR(64) NOT NULL,
    guild_level INTEGER DEFAULT 1,
    guild_xp BIGINT DEFAULT 0,
    member_count INTEGER DEFAULT 1,
    max_members INTEGER DEFAULT 20,
    created_at TIMESTAMPTZ DEFAULT now(),
    symbol_data BYTEA DEFAULT create_default_guild_symbol() NOT NULL,
    is_disbanded BOOLEAN DEFAULT false,
    CONSTRAINT guilds_pkey PRIMARY KEY (guild_id)
);

-- Table: guild_invites
CREATE TABLE IF NOT EXISTS guild_invites (
    invite_id SERIAL NOT NULL,
    guild_id INTEGER NOT NULL,
    inviter_character_id VARCHAR(64) NOT NULL,
    invitee_character_id VARCHAR(64) NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now(),
    expires_at TIMESTAMPTZ DEFAULT (now() + '24:00:00'::interval),
    CONSTRAINT guild_invites_pkey PRIMARY KEY (invite_id)
);

-- Table: guild_level_requirements
CREATE TABLE IF NOT EXISTS guild_level_requirements (
    level INTEGER NOT NULL,
    xp_for_level BIGINT NOT NULL,
    xp_cumulative BIGINT NOT NULL,
    CONSTRAINT guild_level_requirements_pkey PRIMARY KEY (level)
);

-- Table: guild_members
CREATE TABLE IF NOT EXISTS guild_members (
    character_id VARCHAR(64) NOT NULL,
    guild_id INTEGER NOT NULL,
    rank SMALLINT DEFAULT 0,
    xp_contributed BIGINT DEFAULT 0,
    joined_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT guild_members_pkey PRIMARY KEY (character_id)
);

-- Table: guild_rankings
CREATE TABLE IF NOT EXISTS guild_rankings (
    guild_id INTEGER,
    guild_name VARCHAR(24),
    guild_level INTEGER,
    guild_xp BIGINT,
    member_count INTEGER,
    max_members INTEGER,
    owner_character_id VARCHAR(64),
    owner_name VARCHAR(64),
    rank_position BIGINT
);

-- Table: guild_symbol_palette
CREATE TABLE IF NOT EXISTS guild_symbol_palette (
    palette_index SMALLINT NOT NULL,
    color_name VARCHAR(32) NOT NULL,
    color_hex CHARACTER NOT NULL,
    color_r SMALLINT NOT NULL,
    color_g SMALLINT NOT NULL,
    color_b SMALLINT NOT NULL,
    color_a SMALLINT DEFAULT 255 NOT NULL,
    CONSTRAINT guild_symbol_palette_pkey PRIMARY KEY (palette_index)
);

-- Table: honor_rankings
CREATE TABLE IF NOT EXISTS honor_rankings (
    character_id VARCHAR(64),
    character_name VARCHAR(64),
    class_name VARCHAR(32),
    level INTEGER,
    honor INTEGER,
    pvp_kills INTEGER,
    pvp_deaths INTEGER,
    kd_ratio DOUBLE PRECISION,
    honor_rank BIGINT
);

-- Table: honor_rankings_top100
CREATE TABLE IF NOT EXISTS honor_rankings_top100 (
    character_id VARCHAR(64),
    character_name VARCHAR(64),
    class_name VARCHAR(32),
    level INTEGER,
    honor INTEGER,
    pvp_kills INTEGER,
    pvp_deaths INTEGER,
    kd_ratio DOUBLE PRECISION,
    honor_rank BIGINT
);

-- Table: inventory
CREATE TABLE IF NOT EXISTS inventory (
    inventory_id SERIAL NOT NULL,
    character_id VARCHAR(64) NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    slot_index INTEGER NOT NULL,
    quantity INTEGER DEFAULT 1,
    durability INTEGER DEFAULT 100,
    enhancement_level INTEGER DEFAULT 0,
    is_equipped BOOLEAN DEFAULT false,
    created_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT inventory_pkey PRIMARY KEY (inventory_id)
);

-- Table: jackpot_history
CREATE TABLE IF NOT EXISTS jackpot_history (
    payout_id SERIAL NOT NULL,
    winner_character_id VARCHAR(64),
    winner_character_name VARCHAR(64),
    amount BIGINT NOT NULL,
    paid_at TIMESTAMPTZ DEFAULT now() NOT NULL,
    is_rollover BOOLEAN DEFAULT false,
    CONSTRAINT jackpot_history_pkey PRIMARY KEY (payout_id)
);

-- Table: jackpot_pool
CREATE TABLE IF NOT EXISTS jackpot_pool (
    id INTEGER DEFAULT 1 NOT NULL,
    current_pool BIGINT DEFAULT 0 NOT NULL,
    next_payout_at TIMESTAMPTZ DEFAULT (now() + '02:00:00'::interval) NOT NULL,
    last_updated_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT jackpot_pool_pkey PRIMARY KEY (id)
);

-- Table: loot_tables
CREATE TABLE IF NOT EXISTS loot_tables (
    loot_table_id VARCHAR(64) NOT NULL,
    description TEXT,
    CONSTRAINT loot_tables_pkey PRIMARY KEY (loot_table_id)
);

-- Table: loot_drops
CREATE TABLE IF NOT EXISTS loot_drops (
    drop_id SERIAL NOT NULL,
    loot_table_id VARCHAR(64) NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    min_quantity INTEGER DEFAULT 1,
    max_quantity INTEGER DEFAULT 1,
    drop_chance DOUBLE PRECISION NOT NULL,
    condition_req VARCHAR(64),
    CONSTRAINT loot_drops_pkey PRIMARY KEY (drop_id)
);

-- Table: market_listings
CREATE TABLE IF NOT EXISTS market_listings (
    listing_id SERIAL NOT NULL,
    seller_character_id VARCHAR(64) NOT NULL,
    seller_character_name VARCHAR(64) NOT NULL,
    item_instance_id UUID NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    quantity INTEGER DEFAULT 1 NOT NULL,
    rolled_stats JSONB DEFAULT '{}'::jsonb NOT NULL,
    socket_stat VARCHAR(16),
    socket_value INTEGER,
    enchant_level INTEGER DEFAULT 0,
    price_gold BIGINT NOT NULL,
    listed_at TIMESTAMPTZ DEFAULT now() NOT NULL,
    expires_at TIMESTAMPTZ DEFAULT (now() + '2 days'::interval) NOT NULL,
    item_name VARCHAR(100) NOT NULL,
    item_category VARCHAR(32),
    item_subtype VARCHAR(32),
    item_rarity VARCHAR(32) DEFAULT 'Common'::character varying,
    item_level INTEGER DEFAULT 1,
    is_active BOOLEAN DEFAULT true,
    CONSTRAINT market_listings_pkey PRIMARY KEY (listing_id)
);

-- Table: market_transactions
CREATE TABLE IF NOT EXISTS market_transactions (
    transaction_id SERIAL NOT NULL,
    listing_id INTEGER NOT NULL,
    seller_character_id VARCHAR(64) NOT NULL,
    seller_character_name VARCHAR(64) NOT NULL,
    buyer_character_id VARCHAR(64) NOT NULL,
    buyer_character_name VARCHAR(64) NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    item_name VARCHAR(100) NOT NULL,
    quantity INTEGER NOT NULL,
    enchant_level INTEGER DEFAULT 0,
    rolled_stats JSONB,
    sale_price BIGINT NOT NULL,
    tax_amount BIGINT NOT NULL,
    seller_received BIGINT NOT NULL,
    sold_at TIMESTAMPTZ DEFAULT now() NOT NULL,
    CONSTRAINT market_transactions_pkey PRIMARY KEY (transaction_id)
);

-- Table: mob_definitions
CREATE TABLE IF NOT EXISTS mob_definitions (
    mob_def_id VARCHAR(64) NOT NULL,
    mob_name VARCHAR(64) NOT NULL,
    base_hp INTEGER DEFAULT 50,
    base_damage INTEGER DEFAULT 10,
    base_armor INTEGER DEFAULT 0,
    crit_rate REAL DEFAULT 0.05,
    attack_speed REAL DEFAULT 1.0,
    move_speed REAL DEFAULT 3.0,
    base_xp_reward INTEGER DEFAULT 10,
    xp_per_level INTEGER DEFAULT 2,
    loot_table_id VARCHAR(64),
    aggro_range REAL DEFAULT 5.0,
    attack_range REAL DEFAULT 1.5,
    leash_radius REAL DEFAULT 10.0,
    respawn_seconds INTEGER DEFAULT 300,
    hp_per_level REAL DEFAULT 5.0,
    damage_per_level REAL DEFAULT 1.0,
    min_spawn_level INTEGER DEFAULT 1,
    max_spawn_level INTEGER DEFAULT 99,
    is_boss BOOLEAN DEFAULT false,
    is_elite BOOLEAN DEFAULT false,
    created_at TIMESTAMPTZ DEFAULT now(),
    display_name VARCHAR(128),
    armor_per_level REAL DEFAULT 0.5,
    is_aggressive BOOLEAN DEFAULT true,
    spawn_weight INTEGER DEFAULT 10,
    attack_style VARCHAR(16) DEFAULT 'Melee'::character varying,
    monster_type VARCHAR(16) DEFAULT 'Normal'::character varying,
    min_gold_drop INTEGER DEFAULT 0,
    max_gold_drop INTEGER DEFAULT 0,
    gold_drop_chance REAL DEFAULT 1.0,
    prefab_name VARCHAR(128),
    zone_name VARCHAR(50),
    magic_resist INTEGER DEFAULT 0,
    deals_magic_damage BOOLEAN DEFAULT false,
    mob_hit_rate INTEGER DEFAULT 0,
    honor_reward INTEGER DEFAULT 0,
    CONSTRAINT mob_definitions_pkey PRIMARY KEY (mob_def_id)
);

-- Table: npc_vendors
CREATE TABLE IF NOT EXISTS npc_vendors (
    vendor_id VARCHAR(64) NOT NULL,
    display_name VARCHAR(128) NOT NULL,
    zone VARCHAR(64) NOT NULL,
    description TEXT,
    required_level INTEGER DEFAULT 1,
    position_x DOUBLE PRECISION DEFAULT 0,
    position_y DOUBLE PRECISION DEFAULT 0,
    scene_name VARCHAR(64),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT npc_vendors_pkey PRIMARY KEY (vendor_id)
);

-- Table: parties
CREATE TABLE IF NOT EXISTS parties (
    party_id SERIAL NOT NULL,
    leader_character_id VARCHAR(64) NOT NULL,
    created_at TIMESTAMPTZ DEFAULT now(),
    max_members INTEGER DEFAULT 3,
    loot_mode SMALLINT DEFAULT 0 NOT NULL,
    CONSTRAINT parties_pkey PRIMARY KEY (party_id)
);

-- Table: party_invites
CREATE TABLE IF NOT EXISTS party_invites (
    invite_id SERIAL NOT NULL,
    from_character_id VARCHAR(64) NOT NULL,
    to_character_id VARCHAR(64) NOT NULL,
    party_id INTEGER,
    created_at TIMESTAMPTZ DEFAULT now(),
    expires_at TIMESTAMPTZ DEFAULT (now() + '00:01:00'::interval),
    CONSTRAINT party_invites_pkey PRIMARY KEY (invite_id)
);

-- Table: party_members
CREATE TABLE IF NOT EXISTS party_members (
    party_id INTEGER NOT NULL,
    character_id VARCHAR(64) NOT NULL,
    joined_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT party_members_pkey PRIMARY KEY (party_id, character_id)
);

-- Table: player_rankings_archer
CREATE TABLE IF NOT EXISTS player_rankings_archer (
    character_id VARCHAR(64),
    character_name VARCHAR(64),
    level INTEGER,
    current_xp BIGINT,
    guild_id INTEGER,
    guild_name VARCHAR(24),
    rank_position BIGINT
);

-- Table: player_rankings_global
CREATE TABLE IF NOT EXISTS player_rankings_global (
    character_id VARCHAR(64),
    character_name VARCHAR(64),
    class_name VARCHAR(32),
    level INTEGER,
    current_xp BIGINT,
    guild_id INTEGER,
    guild_name VARCHAR(24),
    rank_position BIGINT
);

-- Table: player_rankings_mage
CREATE TABLE IF NOT EXISTS player_rankings_mage (
    character_id VARCHAR(64),
    character_name VARCHAR(64),
    level INTEGER,
    current_xp BIGINT,
    guild_id INTEGER,
    guild_name VARCHAR(24),
    rank_position BIGINT
);

-- Table: player_rankings_warrior
CREATE TABLE IF NOT EXISTS player_rankings_warrior (
    character_id VARCHAR(64),
    character_name VARCHAR(64),
    level INTEGER,
    current_xp BIGINT,
    guild_id INTEGER,
    guild_name VARCHAR(24),
    rank_position BIGINT
);

-- Table: pvp_kill_tracking
CREATE TABLE IF NOT EXISTS pvp_kill_tracking (
    id SERIAL NOT NULL,
    attacker_id VARCHAR(64) NOT NULL,
    victim_id VARCHAR(64) NOT NULL,
    kill_time TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT pvp_kill_tracking_pkey PRIMARY KEY (id)
);

-- Table: quest_definitions
CREATE TABLE IF NOT EXISTS quest_definitions (
    quest_id VARCHAR(64) NOT NULL,
    quest_name VARCHAR(128) NOT NULL,
    quest_type VARCHAR(32) DEFAULT 'side'::character varying,
    description TEXT,
    level_req INTEGER DEFAULT 1,
    class_req VARCHAR(32),
    zone VARCHAR(64),
    prerequisite_quest VARCHAR(64),
    objective_type VARCHAR(32) NOT NULL,
    objective_target VARCHAR(128),
    objective_count INTEGER DEFAULT 1,
    xp_reward INTEGER DEFAULT 0,
    gold_reward INTEGER DEFAULT 0,
    CONSTRAINT quest_definitions_pkey PRIMARY KEY (quest_id)
);

-- Table: quest_progress
CREATE TABLE IF NOT EXISTS quest_progress (
    progress_id SERIAL NOT NULL,
    character_id VARCHAR(64) NOT NULL,
    quest_id VARCHAR(64) NOT NULL,
    status VARCHAR(32) DEFAULT 'not_started'::character varying,
    current_count INTEGER DEFAULT 0,
    target_count INTEGER DEFAULT 1,
    started_at TIMESTAMPTZ,
    completed_at TIMESTAMPTZ,
    CONSTRAINT quest_progress_pkey PRIMARY KEY (progress_id)
);

-- Table: quest_rewards
CREATE TABLE IF NOT EXISTS quest_rewards (
    reward_id SERIAL NOT NULL,
    quest_id VARCHAR(64) NOT NULL,
    item_id VARCHAR(64) NOT NULL,
    quantity INTEGER DEFAULT 1,
    class_req VARCHAR(32),
    is_choice BOOLEAN DEFAULT false,
    choice_group INTEGER DEFAULT 0,
    CONSTRAINT quest_rewards_pkey PRIMARY KEY (reward_id)
);

-- Table: scenes
CREATE TABLE IF NOT EXISTS scenes (
    scene_id VARCHAR(64) NOT NULL,
    scene_name VARCHAR(128) NOT NULL,
    scene_type VARCHAR(32) DEFAULT 'zone'::character varying,
    description TEXT,
    min_level INTEGER DEFAULT 1,
    max_level INTEGER DEFAULT 99,
    created_at TIMESTAMPTZ DEFAULT now(),
    pvp_enabled BOOLEAN DEFAULT true,
    is_dungeon BOOLEAN DEFAULT false,
    CONSTRAINT scenes_pkey PRIMARY KEY (scene_id)
);

-- Table: session_logs
CREATE TABLE IF NOT EXISTS session_logs (
    log_id SERIAL NOT NULL,
    account_id INTEGER,
    character_id VARCHAR(64),
    event_type VARCHAR(64) NOT NULL,
    event_data JSONB,
    client_ip VARCHAR(45),
    created_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT session_logs_pkey PRIMARY KEY (log_id)
);

-- Table: skill_ranks
CREATE TABLE IF NOT EXISTS skill_ranks (
    skill_id VARCHAR(64) NOT NULL,
    rank INTEGER NOT NULL,
    resource_cost INTEGER DEFAULT 0,
    cooldown_seconds DOUBLE PRECISION DEFAULT 0,
    damage_percent INTEGER DEFAULT 100,
    max_targets INTEGER DEFAULT 1,
    effect_duration DOUBLE PRECISION DEFAULT 0,
    effect_value DOUBLE PRECISION DEFAULT 0,
    effect_value_2 DOUBLE PRECISION DEFAULT 0,
    stun_duration DOUBLE PRECISION DEFAULT 0,
    execute_threshold DOUBLE PRECISION DEFAULT 0,
    passive_damage_reduction DOUBLE PRECISION DEFAULT 0,
    passive_crit_bonus DOUBLE PRECISION DEFAULT 0,
    passive_speed_bonus DOUBLE PRECISION DEFAULT 0,
    passive_hp_bonus DOUBLE PRECISION DEFAULT 0,
    passive_stat_bonus INTEGER DEFAULT 0,
    passive_armor_bonus DOUBLE PRECISION DEFAULT 0,
    passive_hit_rate_bonus DOUBLE PRECISION DEFAULT 0,
    transform_damage_mult DOUBLE PRECISION DEFAULT 1.0,
    transform_speed_bonus DOUBLE PRECISION DEFAULT 0,
    resurrect_hp_percent DOUBLE PRECISION DEFAULT 0,
    CONSTRAINT skill_ranks_pkey PRIMARY KEY (skill_id, rank)
);

-- Table: trade_history
CREATE TABLE IF NOT EXISTS trade_history (
    history_id SERIAL NOT NULL,
    session_id INTEGER NOT NULL,
    player_a_character_id VARCHAR(64) NOT NULL,
    player_b_character_id VARCHAR(64) NOT NULL,
    player_a_gold BIGINT DEFAULT 0,
    player_b_gold BIGINT DEFAULT 0,
    player_a_items JSONB,
    player_b_items JSONB,
    completed_at TIMESTAMPTZ DEFAULT now(),
    CONSTRAINT trade_history_pkey PRIMARY KEY (history_id)
);

-- Table: trade_invites
CREATE TABLE IF NOT EXISTS trade_invites (
    invite_id SERIAL NOT NULL,
    from_character_id VARCHAR(64) NOT NULL,
    to_character_id VARCHAR(64) NOT NULL,
    scene_name VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT now(),
    expires_at TIMESTAMP DEFAULT (now() + '00:00:30'::interval),
    CONSTRAINT trade_invites_pkey PRIMARY KEY (invite_id)
);

-- Table: trade_sessions
CREATE TABLE IF NOT EXISTS trade_sessions (
    session_id SERIAL NOT NULL,
    player_a_character_id VARCHAR(64) NOT NULL,
    player_b_character_id VARCHAR(64) NOT NULL,
    player_a_locked BOOLEAN DEFAULT false,
    player_b_locked BOOLEAN DEFAULT false,
    player_a_confirmed BOOLEAN DEFAULT false,
    player_b_confirmed BOOLEAN DEFAULT false,
    player_a_gold BIGINT DEFAULT 0,
    player_b_gold BIGINT DEFAULT 0,
    status VARCHAR(20) DEFAULT 'pending'::character varying,
    scene_name VARCHAR(64) NOT NULL,
    created_at TIMESTAMP DEFAULT now(),
    completed_at TIMESTAMP,
    CONSTRAINT trade_sessions_pkey PRIMARY KEY (session_id)
);

-- Table: trade_offers
CREATE TABLE IF NOT EXISTS trade_offers (
    offer_id SERIAL NOT NULL,
    session_id INTEGER NOT NULL,
    character_id VARCHAR(64) NOT NULL,
    slot_index INTEGER NOT NULL,
    inventory_source_slot INTEGER NOT NULL,
    item_instance_id UUID NOT NULL,
    quantity INTEGER DEFAULT 1,
    CONSTRAINT trade_offers_pkey PRIMARY KEY (offer_id)
);

-- Table: vendor_inventory
CREATE TABLE IF NOT EXISTS vendor_inventory (
    id SERIAL NOT NULL,
    vendor_id VARCHAR(64),
    item_id VARCHAR(64),
    price_gold INTEGER NOT NULL,
    required_level INTEGER DEFAULT 1,
    stock_limit INTEGER DEFAULT '-1'::integer,
    restock_hours INTEGER DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT vendor_inventory_pkey PRIMARY KEY (id)
);

-- Table: zone_mob_deaths
CREATE TABLE IF NOT EXISTS zone_mob_deaths (
    id SERIAL NOT NULL,
    scene_name VARCHAR(64) NOT NULL,
    zone_name VARCHAR(64) NOT NULL,
    enemy_id VARCHAR(64) NOT NULL,
    mob_index INTEGER NOT NULL,
    died_at_unix BIGINT NOT NULL,
    respawn_seconds INTEGER NOT NULL,
    CONSTRAINT zone_mob_deaths_pkey PRIMARY KEY (id)
);

-- =============================================================================
-- Foreign Key Constraints
-- =============================================================================

ALTER TABLE blocked_players ADD CONSTRAINT fk_blocker FOREIGN KEY (blocker_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE blocked_players ADD CONSTRAINT fk_blocked FOREIGN KEY (blocked_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE bounties ADD CONSTRAINT bounties_target_character_id_fkey FOREIGN KEY (target_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE bounty_contributions ADD CONSTRAINT bounty_contributions_contributor_character_id_fkey FOREIGN KEY (contributor_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE bounty_contributions ADD CONSTRAINT bounty_contributions_bounty_id_fkey FOREIGN KEY (bounty_id) REFERENCES bounties (bounty_id) ON DELETE CASCADE;
ALTER TABLE character_bags ADD CONSTRAINT character_bags_character_id_fkey FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE character_bags ADD CONSTRAINT character_bags_bag_id_fkey FOREIGN KEY (bag_id) REFERENCES bag_definitions (bag_id);
ALTER TABLE character_inventory ADD CONSTRAINT character_inventory_character_id_fkey FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE character_inventory ADD CONSTRAINT character_inventory_item_id_fkey FOREIGN KEY (item_id) REFERENCES item_definitions (item_id);
ALTER TABLE character_skill_bar ADD CONSTRAINT character_skill_bar_character_id_fkey FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE character_skill_points ADD CONSTRAINT fk_skill_points_character FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE character_skills ADD CONSTRAINT character_skills_skill_id_fkey FOREIGN KEY (skill_id) REFERENCES skill_definitions (skill_id) ON DELETE CASCADE;
ALTER TABLE character_skills ADD CONSTRAINT character_skills_character_id_fkey FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE characters ADD CONSTRAINT characters_account_id_fkey FOREIGN KEY (account_id) REFERENCES accounts (account_id) ON DELETE CASCADE;
ALTER TABLE crafting_ingredients ADD CONSTRAINT crafting_ingredients_recipe_id_fkey FOREIGN KEY (recipe_id) REFERENCES crafting_recipes (recipe_id) ON DELETE CASCADE;
ALTER TABLE friends ADD CONSTRAINT fk_friend_friend FOREIGN KEY (friend_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE friends ADD CONSTRAINT fk_friend_character FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE gauntlet_performance_rewards ADD CONSTRAINT gauntlet_performance_rewards_division_id_fkey FOREIGN KEY (division_id) REFERENCES gauntlet_config (division_id) ON DELETE CASCADE;
ALTER TABLE gauntlet_player_results ADD CONSTRAINT gauntlet_player_results_result_id_fkey FOREIGN KEY (result_id) REFERENCES gauntlet_results (result_id) ON DELETE CASCADE;
ALTER TABLE gauntlet_rewards ADD CONSTRAINT gauntlet_rewards_division_id_fkey FOREIGN KEY (division_id) REFERENCES gauntlet_config (division_id) ON DELETE CASCADE;
ALTER TABLE gauntlet_waves ADD CONSTRAINT gauntlet_waves_division_id_fkey FOREIGN KEY (division_id) REFERENCES gauntlet_config (division_id) ON DELETE CASCADE;
ALTER TABLE guild_invites ADD CONSTRAINT guild_invites_guild_id_fkey FOREIGN KEY (guild_id) REFERENCES guilds (guild_id) ON DELETE CASCADE;
ALTER TABLE guild_members ADD CONSTRAINT guild_members_guild_id_fkey FOREIGN KEY (guild_id) REFERENCES guilds (guild_id) ON DELETE CASCADE;
ALTER TABLE inventory ADD CONSTRAINT inventory_character_id_fkey FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE loot_drops ADD CONSTRAINT fk_loot_drops_item FOREIGN KEY (item_id) REFERENCES item_definitions (item_id) ON DELETE CASCADE;
ALTER TABLE loot_drops ADD CONSTRAINT loot_drops_loot_table_id_fkey FOREIGN KEY (loot_table_id) REFERENCES loot_tables (loot_table_id) ON DELETE CASCADE;
ALTER TABLE market_listings ADD CONSTRAINT market_listings_seller_character_id_fkey FOREIGN KEY (seller_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE parties ADD CONSTRAINT fk_party_leader FOREIGN KEY (leader_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE party_invites ADD CONSTRAINT fk_invite_party FOREIGN KEY (party_id) REFERENCES parties (party_id) ON DELETE CASCADE;
ALTER TABLE party_invites ADD CONSTRAINT fk_invite_from FOREIGN KEY (from_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE party_invites ADD CONSTRAINT fk_invite_to FOREIGN KEY (to_character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE party_members ADD CONSTRAINT fk_party_member_party FOREIGN KEY (party_id) REFERENCES parties (party_id) ON DELETE CASCADE;
ALTER TABLE party_members ADD CONSTRAINT fk_party_member_character FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE pvp_kill_tracking ADD CONSTRAINT fk_victim FOREIGN KEY (victim_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE pvp_kill_tracking ADD CONSTRAINT fk_attacker FOREIGN KEY (attacker_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE quest_definitions ADD CONSTRAINT quest_definitions_prerequisite_quest_fkey FOREIGN KEY (prerequisite_quest) REFERENCES quest_definitions (quest_id);
ALTER TABLE quest_progress ADD CONSTRAINT quest_progress_character_id_fkey FOREIGN KEY (character_id) REFERENCES characters (character_id) ON DELETE CASCADE;
ALTER TABLE quest_rewards ADD CONSTRAINT quest_rewards_quest_id_fkey FOREIGN KEY (quest_id) REFERENCES quest_definitions (quest_id) ON DELETE CASCADE;
ALTER TABLE session_logs ADD CONSTRAINT session_logs_character_id_fkey FOREIGN KEY (character_id) REFERENCES characters (character_id);
ALTER TABLE session_logs ADD CONSTRAINT session_logs_account_id_fkey FOREIGN KEY (account_id) REFERENCES accounts (account_id);
ALTER TABLE skill_ranks ADD CONSTRAINT skill_ranks_skill_id_fkey FOREIGN KEY (skill_id) REFERENCES skill_definitions (skill_id) ON DELETE CASCADE;
ALTER TABLE trade_offers ADD CONSTRAINT trade_offers_session_id_fkey FOREIGN KEY (session_id) REFERENCES trade_sessions (session_id) ON DELETE CASCADE;
ALTER TABLE vendor_inventory ADD CONSTRAINT vendor_inventory_vendor_id_fkey FOREIGN KEY (vendor_id) REFERENCES npc_vendors (vendor_id);
ALTER TABLE vendor_inventory ADD CONSTRAINT vendor_inventory_item_id_fkey FOREIGN KEY (item_id) REFERENCES item_definitions (item_id);

-- =============================================================================
-- Indexes
-- =============================================================================

CREATE UNIQUE INDEX IF NOT EXISTS accounts_email_key ON public.accounts USING btree (email);
CREATE UNIQUE INDEX IF NOT EXISTS accounts_username_key ON public.accounts USING btree (username);
CREATE INDEX IF NOT EXISTS idx_accounts_email ON public.accounts USING btree (email);
CREATE INDEX IF NOT EXISTS idx_accounts_username ON public.accounts USING btree (username);
CREATE INDEX IF NOT EXISTS idx_blocked_blocked ON public.blocked_players USING btree (blocked_character_id);
CREATE INDEX IF NOT EXISTS idx_bounties_active ON public.bounties USING btree (is_active, expires_at);
CREATE INDEX IF NOT EXISTS idx_bounties_expires ON public.bounties USING btree (expires_at) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_bounties_target ON public.bounties USING btree (target_character_id) WHERE (is_active = true);
CREATE UNIQUE INDEX IF NOT EXISTS unique_active_target ON public.bounties USING btree (target_character_id);
CREATE INDEX IF NOT EXISTS idx_bounty_claims_claimer ON public.bounty_claim_log USING btree (claimer_character_id, claimed_at DESC);
CREATE INDEX IF NOT EXISTS idx_contributions_bounty ON public.bounty_contributions USING btree (bounty_id);
CREATE INDEX IF NOT EXISTS idx_contributions_contributor ON public.bounty_contributions USING btree (contributor_character_id);
CREATE INDEX IF NOT EXISTS idx_bounty_history_actor ON public.bounty_history USING btree (actor_character_id, occurred_at DESC);
CREATE INDEX IF NOT EXISTS idx_bounty_history_target ON public.bounty_history USING btree (target_character_id, occurred_at DESC);
CREATE INDEX IF NOT EXISTS idx_bounty_history_time ON public.bounty_history USING btree (occurred_at DESC);
CREATE INDEX IF NOT EXISTS idx_bounty_history_type ON public.bounty_history USING btree (event_type);
CREATE INDEX IF NOT EXISTS idx_cached_class_rankings_rank ON public.cached_class_rankings USING btree (class_name, rank_position);
CREATE INDEX IF NOT EXISTS idx_cached_guild_rankings_rank ON public.cached_guild_rankings USING btree (rank_position);
CREATE INDEX IF NOT EXISTS idx_cached_player_rankings_rank ON public.cached_player_rankings USING btree (rank_position);
CREATE UNIQUE INDEX IF NOT EXISTS character_bags_character_id_slot_index_key ON public.character_bags USING btree (character_id, slot_index);
CREATE INDEX IF NOT EXISTS idx_char_bags_character ON public.character_bags USING btree (character_id);
CREATE INDEX IF NOT EXISTS idx_char_inv_bag ON public.character_inventory USING btree (character_id, bag_slot_index) WHERE (bag_slot_index IS NOT NULL);
CREATE INDEX IF NOT EXISTS idx_char_inv_character ON public.character_inventory USING btree (character_id);
CREATE INDEX IF NOT EXISTS idx_char_inv_equipped ON public.character_inventory USING btree (character_id, is_equipped) WHERE (is_equipped = true);
CREATE INDEX IF NOT EXISTS idx_char_inv_slot ON public.character_inventory USING btree (character_id, slot_index) WHERE (slot_index IS NOT NULL);
CREATE INDEX IF NOT EXISTS idx_skill_points_character ON public.character_skill_points USING btree (character_id);
CREATE INDEX IF NOT EXISTS idx_character_skills_character ON public.character_skills USING btree (character_id);
CREATE INDEX IF NOT EXISTS characters_account_id_idx ON public.characters USING btree (account_id);
CREATE UNIQUE INDEX IF NOT EXISTS characters_character_name_lower_uq ON public.characters USING btree (lower((character_name)::text));
CREATE UNIQUE INDEX IF NOT EXISTS characters_character_name_uq ON public.characters USING btree (character_name);
CREATE INDEX IF NOT EXISTS idx_characters_account ON public.characters USING btree (account_id);
CREATE INDEX IF NOT EXISTS idx_characters_class_ranking ON public.characters USING btree (class_name, level DESC, current_xp DESC);
CREATE INDEX IF NOT EXISTS idx_characters_guild ON public.characters USING btree (guild_id);
CREATE INDEX IF NOT EXISTS idx_characters_guild_left ON public.characters USING btree (guild_left_at) WHERE (guild_left_at IS NOT NULL);
CREATE INDEX IF NOT EXISTS idx_characters_honor ON public.characters USING btree (honor DESC);
CREATE INDEX IF NOT EXISTS idx_characters_last_online ON public.characters USING btree (last_online DESC NULLS LAST);
CREATE INDEX IF NOT EXISTS idx_characters_merchant_pass ON public.characters USING btree (merchant_pass_expires_at) WHERE (merchant_pass_expires_at IS NOT NULL);
CREATE INDEX IF NOT EXISTS idx_characters_name ON public.characters USING btree (character_name);
CREATE INDEX IF NOT EXISTS idx_characters_pvp_kills ON public.characters USING btree (pvp_kills DESC);
CREATE INDEX IF NOT EXISTS idx_characters_ranking ON public.characters USING btree (level DESC, current_xp DESC);
CREATE INDEX IF NOT EXISTS idx_characters_scene ON public.characters USING btree (current_scene);
CREATE INDEX IF NOT EXISTS idx_crafting_ingredients_recipe ON public.crafting_ingredients USING btree (recipe_id);
CREATE INDEX IF NOT EXISTS idx_crafting_recipes_result ON public.crafting_recipes USING btree (result_item_id);
CREATE INDEX IF NOT EXISTS idx_friends_friend ON public.friends USING btree (friend_character_id);
CREATE INDEX IF NOT EXISTS idx_friends_status ON public.friends USING btree (character_id, status);
CREATE INDEX IF NOT EXISTS idx_gauntlet_perf_rewards_division ON public.gauntlet_performance_rewards USING btree (division_id);
CREATE INDEX IF NOT EXISTS idx_gauntlet_player_results_char ON public.gauntlet_player_results USING btree (character_id);
CREATE INDEX IF NOT EXISTS idx_gauntlet_player_results_result ON public.gauntlet_player_results USING btree (result_id);
CREATE INDEX IF NOT EXISTS idx_gauntlet_results_timestamp ON public.gauntlet_results USING btree (match_timestamp);
CREATE INDEX IF NOT EXISTS idx_gauntlet_rewards_division ON public.gauntlet_rewards USING btree (division_id);
CREATE INDEX IF NOT EXISTS idx_gauntlet_waves_division ON public.gauntlet_waves USING btree (division_id);
CREATE INDEX IF NOT EXISTS idx_guild_invites_invitee ON public.guild_invites USING btree (invitee_character_id);
CREATE UNIQUE INDEX IF NOT EXISTS unique_guild_invite ON public.guild_invites USING btree (guild_id, invitee_character_id);
CREATE INDEX IF NOT EXISTS idx_guild_members_guild ON public.guild_members USING btree (guild_id);
CREATE INDEX IF NOT EXISTS idx_guild_members_rank ON public.guild_members USING btree (guild_id, rank DESC);
CREATE INDEX IF NOT EXISTS idx_guilds_owner ON public.guilds USING btree (owner_character_id);
CREATE INDEX IF NOT EXISTS idx_guilds_ranking ON public.guilds USING btree (guild_level DESC, guild_xp DESC) WHERE (is_disbanded = false);
CREATE UNIQUE INDEX IF NOT EXISTS guilds_guild_name_key ON public.guilds USING btree (guild_name);
CREATE INDEX IF NOT EXISTS idx_inventory_character ON public.inventory USING btree (character_id);
CREATE INDEX IF NOT EXISTS idx_inventory_equipped ON public.inventory USING btree (character_id, is_equipped);
CREATE INDEX IF NOT EXISTS inventory_character_id_idx ON public.inventory USING btree (character_id);
CREATE UNIQUE INDEX IF NOT EXISTS inventory_character_id_slot_index_key ON public.inventory USING btree (character_id, slot_index);
CREATE INDEX IF NOT EXISTS idx_jackpot_history_time ON public.jackpot_history USING btree (paid_at DESC);
CREATE INDEX IF NOT EXISTS idx_loot_drops_table_id ON public.loot_drops USING btree (loot_table_id);
CREATE INDEX IF NOT EXISTS idx_listings_active ON public.market_listings USING btree (is_active, expires_at);
CREATE INDEX IF NOT EXISTS idx_listings_category ON public.market_listings USING btree (item_category) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_listings_expires ON public.market_listings USING btree (expires_at) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_listings_level_price ON public.market_listings USING btree (item_level, price_gold) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_listings_name ON public.market_listings USING btree (lower((item_name)::text)) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_listings_rarity ON public.market_listings USING btree (item_rarity) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_listings_recent ON public.market_listings USING btree (listed_at DESC) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_listings_seller ON public.market_listings USING btree (seller_character_id);
CREATE INDEX IF NOT EXISTS idx_listings_subtype ON public.market_listings USING btree (item_subtype) WHERE (is_active = true);
CREATE INDEX IF NOT EXISTS idx_transactions_buyer ON public.market_transactions USING btree (buyer_character_id, sold_at DESC);
CREATE INDEX IF NOT EXISTS idx_transactions_seller ON public.market_transactions USING btree (seller_character_id, sold_at DESC);
CREATE INDEX IF NOT EXISTS idx_transactions_time ON public.market_transactions USING btree (sold_at DESC);
CREATE INDEX IF NOT EXISTS idx_npc_vendors_zone ON public.npc_vendors USING btree (zone);
CREATE INDEX IF NOT EXISTS idx_parties_leader ON public.parties USING btree (leader_character_id);
CREATE INDEX IF NOT EXISTS idx_party_invites_expires ON public.party_invites USING btree (expires_at);
CREATE INDEX IF NOT EXISTS idx_party_invites_to ON public.party_invites USING btree (to_character_id);
CREATE UNIQUE INDEX IF NOT EXISTS unique_pending_invite ON public.party_invites USING btree (from_character_id, to_character_id, party_id);
CREATE INDEX IF NOT EXISTS idx_party_members_character ON public.party_members USING btree (character_id);
CREATE INDEX IF NOT EXISTS idx_pvp_kills_attacker_victim ON public.pvp_kill_tracking USING btree (attacker_id, victim_id);
CREATE INDEX IF NOT EXISTS idx_pvp_kills_time ON public.pvp_kill_tracking USING btree (kill_time);
CREATE INDEX IF NOT EXISTS idx_quest_definitions_zone ON public.quest_definitions USING btree (zone);
CREATE INDEX IF NOT EXISTS idx_quest_progress_character ON public.quest_progress USING btree (character_id);
CREATE INDEX IF NOT EXISTS idx_quest_progress_status ON public.quest_progress USING btree (character_id, status);
CREATE INDEX IF NOT EXISTS quest_progress_character_id_idx ON public.quest_progress USING btree (character_id);
CREATE UNIQUE INDEX IF NOT EXISTS quest_progress_character_id_quest_id_key ON public.quest_progress USING btree (character_id, quest_id);
CREATE INDEX IF NOT EXISTS idx_quest_rewards_quest ON public.quest_rewards USING btree (quest_id);
CREATE INDEX IF NOT EXISTS idx_session_logs_account ON public.session_logs USING btree (account_id);
CREATE INDEX IF NOT EXISTS idx_session_logs_time ON public.session_logs USING btree (created_at);
CREATE INDEX IF NOT EXISTS idx_session_logs_type ON public.session_logs USING btree (event_type);
CREATE INDEX IF NOT EXISTS idx_skill_definitions_class ON public.skill_definitions USING btree (class_req);
CREATE INDEX IF NOT EXISTS idx_skill_definitions_level ON public.skill_definitions USING btree (level_required);
CREATE INDEX IF NOT EXISTS idx_trade_history_date ON public.trade_history USING btree (completed_at);
CREATE INDEX IF NOT EXISTS idx_trade_history_players ON public.trade_history USING btree (player_a_character_id, player_b_character_id);
CREATE INDEX IF NOT EXISTS idx_trade_invites_expires ON public.trade_invites USING btree (expires_at);
CREATE INDEX IF NOT EXISTS idx_trade_invites_to ON public.trade_invites USING btree (to_character_id);
CREATE INDEX IF NOT EXISTS idx_trade_offers_character ON public.trade_offers USING btree (session_id, character_id);
CREATE INDEX IF NOT EXISTS idx_trade_offers_session ON public.trade_offers USING btree (session_id);
CREATE INDEX IF NOT EXISTS idx_trade_sessions_player_a ON public.trade_sessions USING btree (player_a_character_id) WHERE ((status)::text = 'active'::text);
CREATE INDEX IF NOT EXISTS idx_trade_sessions_player_b ON public.trade_sessions USING btree (player_b_character_id) WHERE ((status)::text = 'active'::text);
CREATE INDEX IF NOT EXISTS idx_trade_sessions_status ON public.trade_sessions USING btree (status);
CREATE INDEX IF NOT EXISTS idx_vendor_inventory_item ON public.vendor_inventory USING btree (item_id);
CREATE INDEX IF NOT EXISTS idx_vendor_inventory_vendor ON public.vendor_inventory USING btree (vendor_id);
CREATE UNIQUE INDEX IF NOT EXISTS vendor_inventory_vendor_id_item_id_key ON public.vendor_inventory USING btree (vendor_id, item_id);
CREATE INDEX IF NOT EXISTS idx_zone_mob_deaths_scene_zone ON public.zone_mob_deaths USING btree (scene_name, zone_name);
CREATE UNIQUE INDEX IF NOT EXISTS zone_mob_deaths_scene_name_zone_name_enemy_id_mob_index_key ON public.zone_mob_deaths USING btree (scene_name, zone_name, enemy_id, mob_index);

-- =============================================================================
-- End of migration 001_full_schema.sql
-- Tables: 65  |  Foreign Keys: 44  |  Indexes: 109
-- =============================================================================