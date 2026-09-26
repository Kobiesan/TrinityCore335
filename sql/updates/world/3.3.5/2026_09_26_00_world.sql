-- ============================================================================
-- Add a third reputation faction to creature_onkill_reputation.
--
-- Lets a single kill award reputation with three factions (e.g. killing a
-- Bloodsail pirate both gains Booty Bay and gains Southport). The core change
-- is in ObjectMgr::LoadReputationOnKill and Player::RewardReputation.
--
-- Southport content uses this to pair the custom Southport faction (2000)
-- alongside the stock Booty Bay (21) <-> Bloodsail (87) relationship.
-- ============================================================================

ALTER TABLE `creature_onkill_reputation`
  ADD COLUMN `RewOnKillRepFaction3` smallint NOT NULL DEFAULT '0' AFTER `RewOnKillRepFaction2`,
  ADD COLUMN `MaxStanding3` tinyint NOT NULL DEFAULT '0' AFTER `RewOnKillRepValue2`,
  ADD COLUMN `IsTeamAward3` tinyint NOT NULL DEFAULT '0' AFTER `MaxStanding3`,
  ADD COLUMN `RewOnKillRepValue3` int NOT NULL DEFAULT '0' AFTER `IsTeamAward3`;
