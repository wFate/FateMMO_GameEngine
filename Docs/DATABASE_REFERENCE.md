# Database Reference

## Connection Details

| Field | Value |
|---|---|
| **Host** | DigitalOcean managed PostgreSQL |
| **Port** | 25060 |
| **Database (dev)** | `fate_engine_dev` |
| **Database (old Unity)** | `fate_mmo` |
| **User** | `doadmin` |
| **SSL** | Required (TLSv1.3) |
| **Engine env var** | `DATABASE_URL` (full connection string) |

The C++ engine reads `DATABASE_URL` from the environment in `ServerApp::init()`.

---

## Schema

**Migration files:** `Docs/migrations/`
- `001_phase7_base_schema.sql` — 4 foundation tables (accounts, characters, item_definitions, character_inventory)
- `001_full_schema.sql` — complete 65-table schema
- `002_bank_and_pets.sql` — character_bank, character_bank_gold, character_pets (3 new tables + 2 indexes)
- `003_gauntlet_seed_data.sql` — 3 divisions, 15 waves (real mob IDs), 15 rewards, 6 performance rewards
- `007_is_broken_book_tier.sql` — is_broken + book_tier columns on character_inventory
- `008_pet_definitions.sql` — pet_definitions table + 3 starter pets (wolf, hawk, turtle)
- `009_admin_role.sql` — admin_role column on accounts
- `010_scene_mob_sync.sql` — 5 WhisperingWoods mob definitions + spawn zones
- `011_dungeon_support.sql` — last_dungeon_entry on characters, difficulty_tier on scenes, 3 boss treasure box items

All 68+ tables are live on `fate_engine_dev`. Migrations 007-009 applied; 010-011 pending.

---

## Live Data (Migrated from fate_mmo)

| Table | Rows | Description |
|---|---|---|
| `gauntlet_config` | 3 | Divisions: Novice (1-20), Veteran (21-40), Champion (41-70) |
| `gauntlet_waves` | 15 | 4 basic + 1 boss per division, real mob IDs |
| `gauntlet_rewards` | 15 | Winner (gold+honor+tokens) and loser (gold+tokens) per division |
| `gauntlet_performance_rewards` | 6 | Top mob/PvP killer bonuses per division |
| `item_definitions` | 748 | All weapons, armor, accessories, consumables, materials |
| `mob_definitions` | 73 | All mob types with stats, AI config, loot table IDs |
| `skill_definitions` | 60 | All skills (20 per class: Warrior/Mage/Archer) |
| `skill_ranks` | 174 | Rank 1-3 data per skill (cost, cooldown, damage%, effects) |
| `loot_tables` | 72 | Loot table definitions (one per mob type) |
| `loot_drops` | 835 | Individual drop entries (item, chance, quantity) |
| `scenes` | 3 | Scene metadata (zone type, PvP flag, level range) |

### Migration History (March 18, 2026)

Data exported from `fate_mmo` via `\copy TO CSV` and imported to `fate_engine_dev` via `\copy FROM CSV`:

```
fate_mmo -> /tmp/item_definitions.csv -> fate_engine_dev.item_definitions (748 rows)
fate_mmo -> /tmp/mob_definitions.csv  -> fate_engine_dev.mob_definitions  (73 rows)
fate_mmo -> /tmp/skill_definitions.csv -> fate_engine_dev.skill_definitions (60 rows)
fate_mmo -> /tmp/skill_ranks.csv      -> fate_engine_dev.skill_ranks     (174 rows)
fate_mmo -> /tmp/loot_tables.csv      -> fate_engine_dev.loot_tables     (72 rows)
fate_mmo -> /tmp/loot_drops.csv       -> fate_engine_dev.loot_drops      (835 rows)
fate_mmo -> /tmp/scenes.csv           -> fate_engine_dev.scenes          (3 rows)
```

**FK dependency order:** `loot_tables` must be loaded before `loot_drops` (FK on `loot_table_id`). `item_definitions` must be loaded before `loot_drops` (FK on `item_id`). `skill_definitions` must be loaded before `skill_ranks` (FK on `skill_id`).

---

## Table Schema Quick Reference

### Core Player Tables

**accounts** — `account_id SERIAL PK`, username, password_hash, email, is_banned, ban_reason

**characters** — `character_id VARCHAR(64) PK`, account_id FK, character_name, class_name, level, current_xp, xp_to_next_level, current_scene, position_x/y, current_hp/mp, base_strength/vitality/intelligence/dexterity/wisdom, gold, honor, pvp_kills, pvp_deaths, current_fury, is_dead, death_timestamp, guild_id, guild_left_at, merchant_pass_expires_at, last_online, total_playtime_seconds

**character_inventory** — `instance_id UUID PK`, character_id FK, item_id FK, slot_index, bag_slot_index, bag_item_slot, rolled_stats JSONB, socket_stat, socket_value, enchant_level, is_protected, is_soulbound, is_equipped, equipped_slot, quantity

### Definition Tables (Read-Only Game Data)

**item_definitions** — `item_id VARCHAR(64) PK`, name, type, subtype, class_req, level_req, damage_min/max, armor, attributes JSONB, description, gold_value, max_stack, icon_path, possible_stats JSONB, is_socketable, is_soulbound, rarity, required_level, max_enchant

**mob_definitions** — `mob_def_id VARCHAR(64) PK`, mob_name, display_name, base_hp/damage/armor, crit_rate, attack_speed, move_speed, hp/damage/armor_per_level, base_xp_reward, xp_per_level, aggro_range, attack_range, leash_radius, respawn_seconds, min/max_spawn_level, spawn_weight, is_aggressive, is_boss, is_elite, attack_style, monster_type, loot_table_id, min/max_gold_drop, gold_drop_chance, magic_resist, deals_magic_damage, mob_hit_rate, honor_reward

**skill_definitions** — `skill_id VARCHAR(64) PK`, skill_name, class_req, skill_type, level_required, resource_type, target_type, range_tiles, aoe_radius, damage_type, can_crit, uses_hit_rate, fury_on_hit, is_ultimate, cast_time, channel_time, applies_bleed/burn/poison/slow/taunt/freeze, grants_invulnerability/stun_immunity/crit_guarantee, removes_debuffs, teleport_distance, dash_distance, is_resurrection, locks_movement, consumes_all_resource, scales_with_resource, description, animation_trigger

**skill_ranks** — `(skill_id, rank) PK`, resource_cost, cooldown_seconds, damage_percent, max_targets, effect_duration, effect_value/2, stun_duration, execute_threshold, passive_damage_reduction/crit_bonus/speed_bonus/hp_bonus/stat_bonus/armor_bonus/hit_rate_bonus, transform_damage_mult/speed_bonus, resurrect_hp_percent

**loot_tables** — `loot_table_id VARCHAR(64) PK`, description

**loot_drops** — `drop_id SERIAL PK`, loot_table_id FK, item_id FK, drop_chance, min/max_quantity, condition_req

**scenes** — `scene_id VARCHAR(64) PK`, scene_name, scene_type, description, min_level, max_level, pvp_enabled, is_dungeon

### Skill System Tables

**character_skills** — `(character_id, skill_id) PK`, unlocked_rank, activated_rank, learned_at

**character_skill_points** — `character_id PK`, total_earned, total_spent, updated_at

**character_skill_bar** — `(character_id, slot_index) PK`, skill_id

### Social Tables

**guilds** — `guild_id SERIAL PK`, guild_name, symbol_data BYTEA, owner_character_id, guild_level, guild_xp, member_count, max_members, is_disbanded

**guild_members** — `character_id PK`, guild_id, rank (0=Member, 1=Officer, 2=Owner), xp_contributed, joined_at

**guild_invites** — `invite_id SERIAL PK`, guild_id, inviter_character_id, invitee_character_id, expires_at (24hr)

**friends** — `(character_id, friend_character_id) PK`, status ('pending'/'accepted'), created_at, accepted_at, note

**blocked_players** — `(blocker_character_id, blocked_character_id) PK`, blocked_at, reason

**parties** — `party_id SERIAL PK`, leader_character_id, max_members, loot_mode

**party_members** — `(party_id, character_id) PK`, joined_at

### Economy Tables

**market_listings** — `listing_id SERIAL PK`, seller_character_id, item_instance_id UUID, item_id, quantity, rolled_stats JSONB, socket_stat/value, enchant_level, price_gold, listed_at, expires_at (2 days), item_name/category/subtype/rarity/level, is_active

**market_transactions** — `transaction_id SERIAL PK`, listing_id, seller/buyer IDs and names, item details, sale_price, tax_amount, seller_received, sold_at

**jackpot_pool** — `id=1 PK`, current_pool, next_payout_at, last_updated_at

**jackpot_history** — `payout_id SERIAL PK`, winner_character_id/name, amount, paid_at, is_rollover

**trade_sessions** — `session_id SERIAL PK`, player_a/b_character_id, player_a/b_locked/confirmed, player_a/b_gold, status, scene_name

**trade_offers** — `offer_id SERIAL PK`, session_id, character_id, slot_index, inventory_source_slot, item_instance_id UUID, quantity

**trade_invites** — `invite_id SERIAL PK`, from/to_character_id, scene_name, expires_at (30s)

**trade_history** — `history_id SERIAL PK`, session_id, player_a/b IDs, gold amounts, items JSONB

### Bounty Tables

**bounties** — `bounty_id SERIAL PK`, target_character_id/name, total_amount, created_at, expires_at (2 days), is_active

**bounty_contributions** — `contribution_id SERIAL PK`, bounty_id, contributor_character_id/name, amount, is_cancelled

**bounty_claim_log** — `claim_id SERIAL PK`, claimer_character_id, target_character_id, amount_claimed, claimed_at

**bounty_history** — `history_id SERIAL PK`, event_type, target/actor IDs and names, amount, tax_amount, party_size, amount_per_member, metadata JSONB

### Gauntlet Tables

**gauntlet_config** — `division_id SERIAL PK`, division_name, min/max_level, arena_scene_name, wave_count, seconds_between_waves, respawn_seconds, team_spawn_a/b_x/y, min_players_to_start, max_players_per_team

**gauntlet_waves** — `wave_id SERIAL PK`, division_id, wave_number, mob_def_id, mob_count, spawn_delay_seconds, is_boss, bonus_points

**gauntlet_rewards** — `reward_id SERIAL PK`, division_id, is_winner, reward_type, reward_value, quantity

**gauntlet_performance_rewards** — `reward_id SERIAL PK`, division_id, category, reward_type, reward_value, quantity

**gauntlet_results** — `result_id SERIAL PK`, division_id, match_timestamp, winning_team, scores, player_count, duration

**gauntlet_player_results** — `id SERIAL PK`, result_id, character_id/name, team, mob/player_kills, deaths, points, was_winner

### Ranking Tables/Views

**cached_player_rankings** — character_id PK, character_name, class_name, level, current_xp, guild_id/name, rank_position

**cached_class_rankings** — (class_name, character_id) PK, same fields filtered by class

**cached_guild_rankings** — guild_id PK, guild_name, guild_level, guild_xp, member_count, owner_name, rank_position

**player_rankings_global/warrior/mage/archer** — views with rank_position computed via RANK() OVER

**guild_rankings** — view with rank by guild_level/xp

**honor_rankings / honor_rankings_top100** — views ranked by honor

### Quest Tables

**quest_definitions** — `quest_id VARCHAR(64) PK`, quest_name, quest_type, description, level_req, class_req, zone, prerequisite_quest, objective_type/target/count, xp/gold_reward

**quest_progress** — `progress_id SERIAL PK`, character_id, quest_id, status, current_count, target_count, started_at, completed_at

**quest_rewards** — `reward_id SERIAL PK`, quest_id, item_id, quantity, class_req, is_choice, choice_group

### Other Tables

**zone_mob_deaths** — `id SERIAL PK`, scene_name, zone_name, enemy_id, mob_index, died_at_unix, respawn_seconds

**session_logs** — `log_id SERIAL PK`, account_id, character_id, event_type, event_data JSONB, client_ip

**pvp_kill_tracking** — `id SERIAL PK`, attacker_id, victim_id, kill_time

**npc_vendors** — vendor_id PK, display_name, zone, position, scene_name

**vendor_inventory** — `id SERIAL PK`, vendor_id, item_id, price_gold, required_level, stock_limit

**crafting_recipes** — recipe_id PK, recipe_name, result_item_id, level_req, gold_cost

**crafting_ingredients** — `ingredient_id SERIAL PK`, recipe_id, item_id, quantity

**bag_definitions** — bag_id PK, name, slot_count, rarity, required_level

**guild_level_requirements** — level PK, xp_for_level, xp_cumulative

**guild_symbol_palette** — palette_index PK, color_name, color_hex, color_r/g/b/a

**character_bank** — `id SERIAL PK`, character_id FK, slot_index (UNIQUE with char), item_id FK, quantity, rolled_stats JSONB, socket_stat, socket_value, enchant_level, is_protected, instance_id UUID

**character_bank_gold** — `character_id PK FK`, stored_gold BIGINT

**character_pets** — `id SERIAL PK`, character_id FK, pet_def_id, pet_name, level, current_xp, is_equipped, is_soulbound, auto_loot_enabled, acquired_at

**character_bags** — `id SERIAL PK`, character_id, slot_index, bag_id

**pet_definitions** — `pet_id VARCHAR(64) PK`, display_name, rarity, base_hp, base_crit_rate, base_exp_bonus, hp_per_level, crit_per_level, exp_bonus_per_level, description, icon_path

**spawn_zones** — `zone_id SERIAL PK`, scene_id, zone_name, center_x, center_y, radius, mob_def_id FK, target_count

---

## C++ Database Layer

### Connection & Async

| File | Purpose |
|---|---|
| `server/db/db_connection.h/.cpp` | Single `pqxx::connection` wrapper with reconnect |
| `server/db/db_pool.h/.cpp` | Connection pool (min 5, max 50, RAII guard) |
| `server/db/db_dispatcher.h` | Async fiber dispatch — `dispatch<Result>(workFn, completionFn)` runs queries on worker fibers, completions drain on game thread |

### Repositories (data access)

| File | Tables | Status |
|---|---|---|
| `server/db/account_repository.h/.cpp` | accounts | DB wired (auth flow) |
| `server/db/character_repository.h/.cpp` | characters | DB wired (connect/disconnect/auto-save) |
| `server/db/inventory_repository.h/.cpp` | character_inventory | DB wired (connect/disconnect) |
| `server/db/skill_repository.h/.cpp` | character_skills, character_skill_bar, character_skill_points | DB wired (connect/disconnect/auto-save) |
| `server/db/guild_repository.h/.cpp` | guilds, guild_members, guild_invites, characters | DB wired (load on connect) |
| `server/db/social_repository.h/.cpp` | friends, blocked_players, characters | DB wired (init + last_online on connect/disconnect) |
| `server/db/market_repository.h/.cpp` | market_listings, market_transactions, jackpot_pool | DB wired (list/buy/cancel handlers, 2% tax, jackpot, expiry) |
| `server/db/trade_repository.h/.cpp` | trade_sessions, trade_offers, trade_history, character_inventory | DB wired (full session flow, atomic item+gold transfer, stale cleanup) |
| `server/db/bounty_repository.h/.cpp` | bounties, bounty_contributions, bounty_claim_log, bounty_history | DB wired (place/cancel handlers + expiry) |
| `server/db/quest_repository.h/.cpp` | quest_progress | DB wired (connect/disconnect + quest commands) |
| `server/db/bank_repository.h/.cpp` | character_bank, character_bank_gold | DB wired (connect/disconnect) |
| `server/db/pet_repository.h/.cpp` | character_pets | DB wired (connect/disconnect) |
| `server/db/zone_mob_state_repository.h/.cpp` | zone_mob_deaths | DB wired (boss respawn) |

### Definition Caches (read-only, loaded at startup)

| File | Tables | Rows |
|---|---|---|
| `server/cache/item_definition_cache.h/.cpp` | item_definitions | 748 |
| `server/cache/loot_table_cache.h/.cpp` | loot_drops, loot_tables | 835 + 72 |
| `server/db/definition_caches.h/.cpp` | mob_definitions, skill_definitions, skill_ranks, scenes | 73 + 60 + 174 + 3 |
| `server/cache/pet_definition_cache.h/.cpp` | pet_definitions | 3 |

### ServerApp Integration

**On startup:** Pool initialized (5-50 connections), 9 repos created, 6 caches loaded (items, loot, mobs, skills, scenes, pets), dispatcher initialized.

**On connect:** Load character + inventory + skills (learned, bar, points) + guild (membership, rank) + friends (init + last_online).

**On disconnect:** Save character + inventory + skills (learned, bar, points) + last_online.

**Every tick:** Drain async dispatcher completions, staggered auto-save (5 min/player), market expiry (60s), bounty expiry (60s), trade cleanup (30s).

**On shutdown:** Save all connected players, shutdown pool, disconnect.

### Query Patterns

```cpp
// All repositories take pqxx::connection& in constructor
CharacterRepository repo(conn);

// All queries use pqxx::work transactions with $1,$2 positional params
pqxx::work txn(conn_);
auto result = txn.exec_params(
    "SELECT * FROM characters WHERE character_id = $1", charId);
txn.commit();

// Null handling: .is_null() on read, std::optional on write
int level = row["level"].is_null() ? 1 : row["level"].as<int>();
std::optional<int> slotIdx = s.slot_index >= 0 ? std::optional(s.slot_index) : std::nullopt;

// Error handling: try-catch with LOG_ERROR
try { ... } catch (const std::exception& e) {
    LOG_ERROR("RepoName", "operation failed: %s", e.what());
}
```
