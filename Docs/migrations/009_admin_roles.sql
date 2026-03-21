-- Migration 009: Admin roles for GM commands
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS admin_role INTEGER DEFAULT 0;
-- 0 = player, 1 = GM, 2 = admin
