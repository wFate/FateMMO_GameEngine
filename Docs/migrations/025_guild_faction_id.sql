-- Guild faction binding: guilds belong to a faction, only same-faction members allowed
ALTER TABLE guilds ADD COLUMN IF NOT EXISTS faction_id SMALLINT NOT NULL DEFAULT 0;

-- Index for queries filtering guilds by faction
CREATE INDEX IF NOT EXISTS idx_guilds_faction_id ON guilds (faction_id) WHERE is_disbanded = false;
