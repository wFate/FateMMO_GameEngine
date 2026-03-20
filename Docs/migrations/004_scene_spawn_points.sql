-- Migration 004: Add default spawn positions to scenes table
-- These are pixel coordinates used by the server for player respawn

ALTER TABLE scenes ADD COLUMN IF NOT EXISTS default_spawn_x REAL DEFAULT 0.0;
ALTER TABLE scenes ADD COLUMN IF NOT EXISTS default_spawn_y REAL DEFAULT 0.0;

-- Set spawn positions matching the SpawnPointComponent entities in scene JSON files
UPDATE scenes SET default_spawn_x = 0.0, default_spawn_y = 0.0 WHERE scene_id = 'Town';
UPDATE scenes SET default_spawn_x = 128.0, default_spawn_y = 128.0 WHERE scene_id = 'WhisperingWoods';
