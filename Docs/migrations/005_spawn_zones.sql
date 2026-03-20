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
