-- 020_items_and_recall.sql
-- Add 9 item definitions (fate coins, soul anchors, tokens, scrolls, beacon,
-- elixir) and a recall_scene column to the characters table.

INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound, level_req, class_req)
VALUES
    ('fate_coin',          'Fate Coin',          'Consumable', 'fate_coin',          'A coin imbued with the essence of fate. Use 3 to gain experience equal to your level times 50.',          500, 9999, 'Common',   false, 1, 'All'),
    ('soul_anchor',        'Soul Anchor',        'Consumable', 'soul_anchor',        'A crystallized fragment of willpower. Automatically consumed on death to prevent experience loss.',     2310,   99, 'Uncommon', false, 1, 'All'),
    ('battle_token',       'Battle Token',       'Currency',   NULL,                 'A token of valor earned on the battlefield. Exchange at special vendors.',                                0, 9999, 'Common',   false, 1, 'All'),
    ('dungeon_token',      'Dungeon Token',      'Currency',   NULL,                 'A relic recovered from the depths. Exchange at special vendors.',                                         0, 9999, 'Common',   false, 1, 'All'),
    ('guild_token',        'Guild Token',        'Currency',   NULL,                 'A mark of guild contribution. Exchange at the Guild Shop.',                                               0, 9999, 'Common',   false, 1, 'All'),
    ('beacon_of_calling',  'Beacon of Calling',  'Consumable', 'beacon_of_calling',  'Summons a party member to your current location.',                                                   10000,    5, 'Rare',     false, 1, 'All')
ON CONFLICT (item_id) DO NOTHING;

INSERT INTO item_definitions (item_id, name, type, subtype, description, gold_value, max_stack, rarity, is_soulbound, level_req, class_req, attributes)
VALUES
    ('minor_exp_scroll',    'Minor EXP Scroll',    'Consumable', 'exp_boost',   'Grants a 10% experience boost for 1 hour.',                                                               5000,  99, 'Common',   false, 1, 'All', '{"exp_boost_percent": 10, "exp_boost_duration": 3600}'),
    ('major_exp_scroll',    'Major EXP Scroll',    'Consumable', 'exp_boost',   'Grants a 20% experience boost for 1 hour.',                                                              15000,  99, 'Uncommon', false, 1, 'All', '{"exp_boost_percent": 20, "exp_boost_duration": 3600}'),
    ('elixir_of_forgetting','Elixir of Forgetting','Consumable', 'stat_reset',  'A bitter draught that erases all memory of combat technique. Resets all spent skill points.',              11000,  10, 'Uncommon', false, 1, 'All', '{}')
ON CONFLICT (item_id) DO NOTHING;

-- Allow players to set a recall destination (defaults to Town).
ALTER TABLE characters ADD COLUMN IF NOT EXISTS recall_scene VARCHAR(64) DEFAULT 'Town';
