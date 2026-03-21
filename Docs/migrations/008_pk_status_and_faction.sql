-- Migration 008: Persist PK status and faction across sessions
-- Run after 007_enchant_and_crafting.sql

-- PK status: 0=White, 1=Purple, 2=Red, 3=Black
ALTER TABLE characters ADD COLUMN IF NOT EXISTS pk_status SMALLINT DEFAULT 0;

-- Faction: 0=None, 1=Xyros, 2=Fenor, 3=Zethos, 4=Solis
ALTER TABLE characters ADD COLUMN IF NOT EXISTS faction SMALLINT DEFAULT 0;
