-- Migration 007: Enchanting break mechanic + crafting book tiers
-- Run after 006_inventory_slot_unique.sql
-- Required for Batch B: Equipment & Economy features

-- Add is_broken flag for enchant break mechanic
ALTER TABLE character_inventory ADD COLUMN IF NOT EXISTS is_broken BOOLEAN DEFAULT FALSE;

-- Add book_tier to crafting recipes for Combine Book tier filtering
ALTER TABLE crafting_recipes ADD COLUMN IF NOT EXISTS book_tier INTEGER DEFAULT 0;

-- Index for recipe tier lookup
CREATE INDEX IF NOT EXISTS idx_crafting_recipes_book_tier ON crafting_recipes(book_tier);
