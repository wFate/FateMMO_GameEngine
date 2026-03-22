-- Migration 011: Dungeon support (daily tickets, difficulty tier, treasure box items)

-- Track daily dungeon ticket usage
ALTER TABLE characters ADD COLUMN IF NOT EXISTS last_dungeon_entry TIMESTAMP;

-- Difficulty tier for gold reward scaling (10,000 gold per tier)
ALTER TABLE scenes ADD COLUMN IF NOT EXISTS difficulty_tier INTEGER DEFAULT 1;
UPDATE scenes SET difficulty_tier = 1 WHERE scene_id = 'GoblinCave';
UPDATE scenes SET difficulty_tier = 2 WHERE scene_id = 'UndeadCrypt';
UPDATE scenes SET difficulty_tier = 3 WHERE scene_id = 'DragonLair';

-- Boss treasure box items (one per tier, consumable type)
INSERT INTO item_definitions (item_id, name, type, subtype, description, rarity, max_stack, gold_value)
VALUES
    ('boss_treasure_box_t1', 'Goblin Hoard', 'Consumable', 'TreasureBox', 'A chest of treasure from the Goblin Cave.', 'Rare', 1, 500),
    ('boss_treasure_box_t2', 'Crypt Reliquary', 'Consumable', 'TreasureBox', 'An ancient reliquary from the Undead Crypt.', 'Epic', 1, 2000),
    ('boss_treasure_box_t3', 'Dragon Hoard', 'Consumable', 'TreasureBox', 'A chest of treasure from the Dragon''s Lair.', 'Legendary', 1, 5000)
ON CONFLICT (item_id) DO NOTHING;
