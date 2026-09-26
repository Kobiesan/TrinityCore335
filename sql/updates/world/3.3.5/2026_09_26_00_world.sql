-- ============================================================================
-- Add a third reputation faction to creature_onkill_reputation.
--
-- Lets a single kill award reputation with three factions (e.g. killing a
-- Bloodsail pirate both gains Booty Bay and gains Southport). The core change
-- is in ObjectMgr::LoadReputationOnKill and Player::RewardReputation.
--
-- Written to be safe to re-run: each column is only added when it is missing
-- (MySQL 8 has no ADD COLUMN IF NOT EXISTS, so this uses a guard).
-- ============================================================================

-- RewOnKillRepFaction3
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'creature_onkill_reputation' AND COLUMN_NAME = 'RewOnKillRepFaction3');
SET @s := IF(@c = 0,
    'ALTER TABLE `creature_onkill_reputation` ADD COLUMN `RewOnKillRepFaction3` smallint NOT NULL DEFAULT ''0'' AFTER `RewOnKillRepFaction2`',
    'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- MaxStanding3
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'creature_onkill_reputation' AND COLUMN_NAME = 'MaxStanding3');
SET @s := IF(@c = 0,
    'ALTER TABLE `creature_onkill_reputation` ADD COLUMN `MaxStanding3` tinyint NOT NULL DEFAULT ''0'' AFTER `RewOnKillRepValue2`',
    'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- IsTeamAward3
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'creature_onkill_reputation' AND COLUMN_NAME = 'IsTeamAward3');
SET @s := IF(@c = 0,
    'ALTER TABLE `creature_onkill_reputation` ADD COLUMN `IsTeamAward3` tinyint NOT NULL DEFAULT ''0'' AFTER `MaxStanding3`',
    'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- RewOnKillRepValue3
SET @c := (SELECT COUNT(*) FROM information_schema.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'creature_onkill_reputation' AND COLUMN_NAME = 'RewOnKillRepValue3');
SET @s := IF(@c = 0,
    'ALTER TABLE `creature_onkill_reputation` ADD COLUMN `RewOnKillRepValue3` int NOT NULL DEFAULT ''0'' AFTER `IsTeamAward3`',
    'SELECT 1');
PREPARE stmt FROM @s; EXECUTE stmt; DEALLOCATE PREPARE stmt;
