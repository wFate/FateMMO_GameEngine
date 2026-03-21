-- Migration 006: Add unique index to prevent duplicate slot assignments
DELETE FROM character_inventory a
USING character_inventory b
WHERE a.ctid < b.ctid
  AND a.character_id = b.character_id
  AND a.slot_index = b.slot_index
  AND a.slot_index IS NOT NULL;

CREATE UNIQUE INDEX uq_character_inventory_slot
  ON character_inventory (character_id, slot_index)
  WHERE slot_index IS NOT NULL;
