-- Migration 005: Spawn zones table for server-side mob spawning
-- Defines which mobs spawn where in each scene

CREATE TABLE IF NOT EXISTS spawn_zones (
    zone_id SERIAL PRIMARY KEY,
    scene_id VARCHAR(64) REFERENCES scenes(scene_id),
    zone_name VARCHAR(128),
    center_x REAL, center_y REAL,    -- pixel coords
    radius REAL,                      -- spawn area radius
    mob_def_id VARCHAR(64) REFERENCES mob_definitions(mob_def_id),
    target_count INT DEFAULT 3,       -- how many of this mob to maintain
    respawn_override_seconds INT      -- NULL = use mob_definitions.respawn_seconds
);

-- WhisperingWoods: 3 sub-zones, level 1-6 progression
INSERT INTO spawn_zones (scene_id, zone_name, center_x, center_y, radius, mob_def_id, target_count) VALUES
('WhisperingWoods', 'Starter Meadow', 0, 0, 150, 'squirrel', 3),
('WhisperingWoods', 'Starter Meadow', 0, 0, 150, 'giant_rat', 2),
('WhisperingWoods', 'Forest Edge', -100, 50, 120, 'horned_hare', 2),
('WhisperingWoods', 'Forest Edge', -100, 50, 120, 'timber_wolf', 2),
('WhisperingWoods', 'Deep Woods', 100, -80, 100, 'grizzly_bear', 2),
('WhisperingWoods', 'Deep Woods', 100, -80, 100, 'timber_alpha', 1);
