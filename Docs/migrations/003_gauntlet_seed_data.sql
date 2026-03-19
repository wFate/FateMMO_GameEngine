-- Migration 003: Gauntlet seed data
-- Applied: March 18, 2026
-- Database: fate_engine_dev
--
-- 3 divisions matching game world zones:
--   Div 1 (Novice): Levels 1-20, forest/coastal mobs, boss: Tidal Serpent
--   Div 2 (Veteran): Levels 21-40, desert/swamp mobs, boss: Pharaoh's Shadow
--   Div 3 (Champion): Levels 41-70, sky/void mobs, boss: Fate Maker

-- ============================================================================
-- Division Configs
-- ============================================================================
INSERT INTO gauntlet_config (division_name, min_level, max_level, arena_scene_name,
    wave_count, seconds_between_waves, respawn_seconds,
    team_spawn_a_x, team_spawn_a_y, team_spawn_b_x, team_spawn_b_y,
    min_players_to_start, max_players_per_team)
VALUES
    ('Novice Arena',   1,  20, 'Gauntlet_Div1', 5, 10, 10, -5.0, 0.0, 5.0, 0.0, 2, 10),
    ('Veteran Arena',  21, 40, 'Gauntlet_Div2', 5, 10, 10, -5.0, 0.0, 5.0, 0.0, 2, 10),
    ('Champion Arena', 41, 70, 'Gauntlet_Div3', 5, 10, 10, -5.0, 0.0, 5.0, 0.0, 2, 10);

-- ============================================================================
-- Waves — Division 1: Novice (forest/coastal creatures, lvl 1-20)
-- ============================================================================
INSERT INTO gauntlet_waves (division_id, wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points) VALUES
    (1, 1, 'horned_hare',      10, 3.0, false, 0),   -- lvl 3-5
    (1, 2, 'timber_wolf',      12, 2.5, false, 0),   -- lvl 4-6
    (1, 3, 'fungus_spitter',   15, 2.0, false, 0),   -- lvl 6-8
    (1, 4, 'deadwood_walker',  18, 1.5, false, 0),   -- lvl 8-10
    (1, 5, 'tidal_serpent',     1, 0.0, true,  50);   -- lvl 20 boss

-- ============================================================================
-- Waves — Division 2: Veteran (desert/swamp creatures, lvl 21-40)
-- ============================================================================
INSERT INTO gauntlet_waves (division_id, wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points) VALUES
    (2, 1, 'sandstorm_elemental', 10, 3.0, false, 0),  -- lvl 31-33
    (2, 2, 'desert_striker',      12, 2.5, false, 0),  -- lvl 33-35
    (2, 3, 'tomb_guardian',       15, 2.0, false, 0),  -- lvl 36-38
    (2, 4, 'cultist_priest',      18, 1.5, false, 0),  -- lvl 38-40
    (2, 5, 'pharaohs_shadow',      1, 0.0, true,  75); -- lvl 40 boss

-- ============================================================================
-- Waves — Division 3: Champion (sky/void creatures, lvl 41-70)
-- ============================================================================
INSERT INTO gauntlet_waves (division_id, wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points) VALUES
    (3, 1, 'plague_walker',      10, 3.0, false, 0),   -- lvl 41-43
    (3, 2, 'storm_harpy',       12, 2.5, false, 0),   -- lvl 54-56
    (3, 3, 'entropy_stalker',   15, 2.0, false, 0),   -- lvl 63-65
    (3, 4, 'void_knight',       18, 1.5, false, 0),   -- lvl 68-70
    (3, 5, 'fate_maker',         1, 0.0, true, 100);   -- lvl 70 boss

-- ============================================================================
-- Rewards: Winners
-- ============================================================================
INSERT INTO gauntlet_rewards (division_id, is_winner, reward_type, reward_value, quantity) VALUES
    -- Div 1 winners
    (1, true, 'Gold',  '5000',  1),
    (1, true, 'Honor', '50',    1),
    (1, true, 'Token', 'gauntlet_token', 3),
    -- Div 2 winners
    (2, true, 'Gold',  '15000', 1),
    (2, true, 'Honor', '100',   1),
    (2, true, 'Token', 'gauntlet_token', 5),
    -- Div 3 winners
    (3, true, 'Gold',  '50000', 1),
    (3, true, 'Honor', '200',   1),
    (3, true, 'Token', 'gauntlet_token', 10);

-- ============================================================================
-- Rewards: Losers (participation)
-- ============================================================================
INSERT INTO gauntlet_rewards (division_id, is_winner, reward_type, reward_value, quantity) VALUES
    (1, false, 'Gold',  '1000',  1),
    (1, false, 'Token', 'gauntlet_token', 1),
    (2, false, 'Gold',  '3000',  1),
    (2, false, 'Token', 'gauntlet_token', 2),
    (3, false, 'Gold',  '10000', 1),
    (3, false, 'Token', 'gauntlet_token', 3);

-- ============================================================================
-- Performance Rewards (top killers per division)
-- ============================================================================
INSERT INTO gauntlet_performance_rewards (division_id, category, reward_type, reward_value, quantity) VALUES
    (1, 'top_mob_killer', 'Token', 'gauntlet_token', 2),
    (1, 'top_pvp_killer', 'Token', 'gauntlet_token', 2),
    (2, 'top_mob_killer', 'Token', 'gauntlet_token', 3),
    (2, 'top_pvp_killer', 'Token', 'gauntlet_token', 3),
    (3, 'top_mob_killer', 'Token', 'gauntlet_token', 5),
    (3, 'top_pvp_killer', 'Token', 'gauntlet_token', 5);
