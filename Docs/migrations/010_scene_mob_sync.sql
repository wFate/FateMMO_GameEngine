-- Migration 010: Add scene-accurate mob definitions and update spawn zones
-- Syncs server spawn data with WhisperingWoods.json scene file mob types
-- Old mobs (squirrel, giant_rat, etc.) remain in DB for other zones

-- Add new mob definitions matching the scene file sprites
INSERT INTO mob_definitions (mob_def_id, mob_name, display_name, base_hp, base_damage, base_armor, base_xp_reward, xp_per_level, hp_per_level, damage_per_level, min_spawn_level, max_spawn_level, aggro_range, attack_range, leash_radius, respawn_seconds, is_aggressive, monster_type, min_gold_drop, max_gold_drop, gold_drop_chance)
VALUES
    ('Slime',        'Slime',        'Slime',        30,  5,  0, 8,  1, 4.0, 0.5, 1, 2, 3.0, 1.0, 8.0, 30, false, 'Normal', 1, 5, 0.8),
    ('Goblin',       'Goblin',       'Goblin',       50,  8,  1, 12, 2, 6.0, 1.0, 2, 3, 5.0, 1.5, 10.0, 30, true,  'Normal', 3, 10, 0.9),
    ('Wolf',         'Wolf',         'Wolf',         70, 12,  1, 18, 3, 8.0, 1.5, 3, 4, 6.0, 1.5, 10.0, 30, true,  'Normal', 5, 15, 0.9),
    ('Mushroom',     'Mushroom',     'Mushroom',     25,  3,  2, 6,  1, 3.0, 0.3, 1, 1, 2.0, 1.0, 6.0, 30, false, 'Normal', 1, 3, 0.5),
    ('Forest_Golem', 'Forest Golem', 'Forest Golem', 150, 18, 5, 40, 5, 15.0, 2.0, 5, 5, 7.0, 2.0, 12.0, 60, true, 'MiniBoss', 20, 50, 1.0)
ON CONFLICT (mob_def_id) DO NOTHING;

-- Update spawn zones to use scene-accurate mob types
DELETE FROM spawn_zones WHERE scene_id = 'WhisperingWoods';

INSERT INTO spawn_zones (scene_id, zone_name, center_x, center_y, radius, mob_def_id, target_count) VALUES
('WhisperingWoods', 'Whispering Woods', 0, 0, 200, 'Slime', 3),
('WhisperingWoods', 'Whispering Woods', 0, 0, 200, 'Goblin', 2),
('WhisperingWoods', 'Whispering Woods', 0, 0, 200, 'Wolf', 2),
('WhisperingWoods', 'Whispering Woods', 0, 0, 200, 'Mushroom', 2),
('WhisperingWoods', 'Whispering Woods', 0, 0, 200, 'Forest_Golem', 1);
