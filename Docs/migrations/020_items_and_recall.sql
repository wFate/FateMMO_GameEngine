-- 020_items_and_recall.sql
-- New items: Fate Coin, Soul Anchor, Battle Token, Dungeon Token, Guild Token,
--            Minor EXP Scroll, Major EXP Scroll, Beacon of Calling, Elixir of Forgetting
-- Also adds recall_scene column to characters table.

-- Fate Coins (late-game XP consumable: 3 coins = level * 50 XP)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound)
VALUES ('fate_coin', 'Fate Coin', 'Consumable', 'fate_coin',
        'A coin imbued with the essence of fate. Use 3 to gain experience equal to your level times 50.',
        500, 9999, 'Common', false)
ON CONFLICT (item_id) DO NOTHING;

-- Soul Anchor (auto-consumed on death to prevent XP loss)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound)
VALUES ('soul_anchor', 'Soul Anchor', 'Consumable', 'soul_anchor',
        'A crystallized fragment of willpower. Automatically consumed on death to prevent experience loss.',
        2310, 99, 'Uncommon', false)
ON CONFLICT (item_id) DO NOTHING;

-- Battle Token (battlefield reward currency)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound)
VALUES ('battle_token', 'Battle Token', 'Currency', '',
        'A token of valor earned on the battlefield. Exchange at special vendors.',
        0, 9999, 'Common', false)
ON CONFLICT (item_id) DO NOTHING;

-- Dungeon Token (dungeon reward currency)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound)
VALUES ('dungeon_token', 'Dungeon Token', 'Currency', '',
        'A relic recovered from the depths. Exchange at special vendors.',
        0, 9999, 'Common', false)
ON CONFLICT (item_id) DO NOTHING;

-- Guild Token (guild activity currency)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound)
VALUES ('guild_token', 'Guild Token', 'Currency', '',
        'A mark of guild contribution. Exchange at the Guild Shop.',
        0, 9999, 'Common', false)
ON CONFLICT (item_id) DO NOTHING;

-- Minor EXP Scroll (10% EXP boost for 1 hour)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound, attributes)
VALUES ('minor_exp_scroll', 'Minor EXP Scroll', 'Consumable', 'exp_boost',
        'Grants a 10% experience boost for 1 hour.',
        5000, 99, 'Common', false,
        '{"exp_boost_percent": 10, "exp_boost_duration": 3600}')
ON CONFLICT (item_id) DO NOTHING;

-- Major EXP Scroll (20% EXP boost for 1 hour)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound, attributes)
VALUES ('major_exp_scroll', 'Major EXP Scroll', 'Consumable', 'exp_boost',
        'Grants a 20% experience boost for 1 hour.',
        15000, 99, 'Uncommon', false,
        '{"exp_boost_percent": 20, "exp_boost_duration": 3600}')
ON CONFLICT (item_id) DO NOTHING;

-- Beacon of Calling (teleport a party member to your location)
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound)
VALUES ('beacon_of_calling', 'Beacon of Calling', 'Consumable', 'beacon_of_calling',
        'Summons a party member to your current location.',
        10000, 5, 'Rare', false)
ON CONFLICT (item_id) DO NOTHING;

-- Elixir of Forgetting (skill point reset — handler already exists for subtype 'stat_reset')
INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound)
VALUES ('elixir_of_forgetting', 'Elixir of Forgetting', 'Consumable', 'stat_reset',
        'A bitter draught that erases all memory of combat technique. Resets all spent skill points.',
        11000, 10, 'Uncommon', false)
ON CONFLICT (item_id) DO NOTHING;

-- Add recall_scene column to characters table
ALTER TABLE characters ADD COLUMN IF NOT EXISTS recall_scene VARCHAR(64) DEFAULT 'Town';
