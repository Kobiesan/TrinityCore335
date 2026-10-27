/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SouthportGuard.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DBCStructure.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GuardAI.h"
#include "MotionMaster.h"
#include "MovementGenerator.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
    // Southport Marine salute exchange: the patrolling marine (1022737) salutes at
    // waypoint 46 of path 90934401, and the two marines posted nearby salute back.
    constexpr uint32 MARINE_SALUTE_WALKER_GUID = 1022737;
    constexpr uint32 MARINE_SALUTE_PATH       = 90934401;
    constexpr uint32 MARINE_SALUTE_POINT      = 46;
    constexpr uint32 MARINE_SALUTE_RESPONDERS[] = { 1022714, 1022713 };

    // a player saluting a marine gets one back, at most once every couple of seconds
    constexpr uint32 SALUTE_COOLDOWN_MS = 2000;
    constexpr uint32 SALUTE_PAUSE_MS    = 1200;   // how long a patrolling marine holds still to salute
    constexpr uint32 SALUTE_RESTORE_MS  = 1600;   // turn back to the original facing once the salute ends

    // Guards are hostile to players carrying PLAYER_FLAGS_CONTESTED_PVP (involved in PvP
    // combat), but the core only re-checks that in MoveInLineOfSight, which fires on
    // movement. Poll instead so a player who starts a fight while standing still is caught.
    constexpr uint32 AGGRO_SCAN_INTERVAL_MS = 1000;
    constexpr float  AGGRO_SCAN_RANGE       = 30.0f;

    // Southport Cannoneer: lobs dynamite at its target during a fight.
    constexpr uint32 SPELL_THROW_DYNAMITE   = 7978;   // stock level-20 spell (+2.5/level), 8 yd splash, 30 yd range
    constexpr float  DYNAMITE_RANGE         = 30.0f;

    // Southport Archer: holds its post and shoots, like the Alterac Valley bowmen.
    constexpr uint32 SPELL_ARCHER_SHOOT     = 500184; // copy of the AV bowmen's Shoot 22121 (80 yd) scaled for level 20 (+3/level)
    constexpr float  ARCHER_SHOOT_RANGE     = 80.0f;
    constexpr float  ARCHER_WATCH_RANGE     = ARCHER_SHOOT_RANGE; // how far a posted archer looks for Southport's attackers
}

struct npc_southport_guard : public GuardAI
{
    npc_southport_guard(Creature* creature) : GuardAI(creature) { }

    void UpdateAI(uint32 diff) override
    {
        if (!me->IsEngaged() && me->IsAlive() && !me->HasReactState(REACT_PASSIVE))
        {
            if (_scanTimer <= diff)
            {
                _scanTimer = AGGRO_SCAN_INTERVAL_MS;
                ScanForContestedPlayers();
            }
            else
                _scanTimer -= diff;
        }

        GuardAI::UpdateAI(diff);
    }

    void WaypointReached(uint32 waypointId, uint32 pathId) override
    {
        if (me->GetSpawnId() != MARINE_SALUTE_WALKER_GUID
            || pathId != MARINE_SALUTE_PATH
            || waypointId != MARINE_SALUTE_POINT)
            return;

        me->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);

        for (uint32 spawnId : MARINE_SALUTE_RESPONDERS)
        {
            Creature* responder = me->GetMap()->GetCreatureBySpawnId(spawnId);
            if (!responder || !responder->IsAlive() || responder->IsInCombat())
                continue;

            responder->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
        }
    }

    // a player saluting a marine gets one back; a patrolling marine pauses briefly to return it,
    // then carries on along its path
    void ReceiveEmote(Player* player, uint32 emoteId) override
    {
        // only a player close enough to be acknowledged gets a salute back
        if (!player || emoteId != TEXT_EMOTE_SALUTE || !me->IsAlive() || me->IsInCombat()
            || !me->IsWithinDistInMap(player, 5.0f))
            return;

        uint32 const now = GameTime::GetGameTimeMS();
        if (now < _saluteReadyAt)
            return;
        _saluteReadyAt = now + SALUTE_COOLDOWN_MS;

        if (MovementGenerator* movement = me->GetMotionMaster()->GetCurrentMovementGenerator())
        {
            MovementGeneratorType const type = movement->GetMovementGeneratorType();
            if (type == WAYPOINT_MOTION_TYPE || type == RANDOM_MOTION_TYPE)
                movement->Pause(SALUTE_PAUSE_MS);
        }

        float const originalOrientation = me->GetOrientation();
        me->SetFacingToObject(player);
        me->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);

        // turn back to the way they were once the salute finishes; a patroller gets re-oriented by
        // its movement generator anyway, but a marine at their post would otherwise stay turned
        Creature* const creature = me;
        me->m_Events.AddEventAtOffset([creature, originalOrientation]()
        {
            // leave it alone if it is already walking again (the path re-orients it)
            if (creature->IsAlive() && !creature->IsInCombat() && !creature->isMoving())
                creature->SetFacingTo(originalOrientation);
        }, Milliseconds(SALUTE_RESTORE_MS));
    }

    void JustDied(Unit* killer) override
    {
        Player* player = killer ? killer->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;

        WorldPacket data(SMSG_ZONE_UNDER_ATTACK, 4);
        data << uint32(me->GetAreaId());

        sWorld->SendGlobalMessage(&data, player ? player->GetSession() : nullptr);
    }

private:
    // Only looks at the grid cells around the marine, not the whole map, so the cost stays
    // flat as the continent population grows.
    void ScanForContestedPlayers()
    {
        std::list<Player*> players;
        Trinity::AnyPlayerInObjectRangeCheck checker(me, AGGRO_SCAN_RANGE);
        Trinity::PlayerListSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(me, players, checker);
        Cell::VisitWorldObjects(me, searcher, AGGRO_SCAN_RANGE);

        for (Player* player : players)
        {
            if (!player->IsAlive())
                continue;

            if (!player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP))
                continue;

            if (!me->CanStartAttack(player, false))
                continue;

            me->EngageWithTarget(player);
            break;
        }
    }

    uint32 _saluteReadyAt = 0;
    uint32 _scanTimer = 0;
};

// Same guard behavior as the marines, plus a thrown stick of dynamite every 9-13 seconds.
struct npc_southport_cannoneer : public npc_southport_guard
{
    npc_southport_cannoneer(Creature* creature) : npc_southport_guard(creature) { }

    void JustEngagedWith(Unit* who) override
    {
        npc_southport_guard::JustEngagedWith(who);
        _dynamiteTimer = urand(2000, 4000);
    }

    void UpdateAI(uint32 diff) override
    {
        npc_southport_guard::UpdateAI(diff);

        Unit* victim = me->GetVictim();
        if (!victim || me->HasUnitState(UNIT_STATE_CASTING))
            return;

        if (_dynamiteTimer <= diff)
        {
            if (me->IsWithinDistInMap(victim, DYNAMITE_RANGE))
            {
                DoCastVictim(SPELL_THROW_DYNAMITE);
                _dynamiteTimer = urand(9000, 13000);
            }
            else
                _dynamiteTimer = 1000;
        }
        else
            _dynamiteTimer -= diff;
    }

private:
    uint32 _dynamiteTimer = 0;
};

// Same guard behavior as the marines, but never leaves its post in combat: it shoots its
// target every 2.3-3.9 seconds out to 80 yards (stock AV bowman timing) and only melees
// whatever reaches it.
struct npc_southport_archer : public npc_southport_guard
{
    npc_southport_archer(Creature* creature) : npc_southport_guard(creature)
    {
        SetCombatMovement(false);
        // random first watch so the posted archers don't all search on the same tick
        _watchTimer = urand(0, AGGRO_SCAN_INTERVAL_MS);
    }

    void JustEngagedWith(Unit* who) override
    {
        npc_southport_guard::JustEngagedWith(who);
        _shootTimer = 0;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!me->IsEngaged() && me->IsAlive() && !me->HasReactState(REACT_PASSIVE))
        {
            if (_watchTimer <= diff)
            {
                _watchTimer = AGGRO_SCAN_INTERVAL_MS;
                WatchFromPost();
            }
            else
                _watchTimer -= diff;
        }

        npc_southport_guard::UpdateAI(diff);

        Unit* victim = me->GetVictim();
        if (!victim || me->HasUnitState(UNIT_STATE_CASTING))
            return;

        if (_shootTimer <= diff)
        {
            if (me->IsWithinDistInMap(victim, ARCHER_SHOOT_RANGE))
                DoCastVictim(SPELL_ARCHER_SHOOT);
            _shootTimer = urand(2300, 3900);
        }
        else
            _shootTimer -= diff;
    }

private:
    // The stock aggro checks never reach the street from a wall post: CanStartAttack rejects
    // anyone more than 3 yards above or below, the aggro radius (~18.5 yards) is shorter than
    // the drop, and the call for help is measured in 3D from the attacked NPC. A player at war
    // with Southport also stays neutral to an idle guard, so look for Southport's attackers
    // directly, anywhere in bow range and in sight.
    void WatchFromPost()
    {
        if (me->IsImmuneToPC())
            return;

        std::list<Player*> players;
        Trinity::AnyPlayerInObjectRangeCheck checker(me, ARCHER_WATCH_RANGE);
        Trinity::PlayerListSearcher<Trinity::AnyPlayerInObjectRangeCheck> searcher(me, players, checker);
        Cell::VisitWorldObjects(me, searcher, ARCHER_WATCH_RANGE);

        for (Player* player : players)
        {
            if (!player->IsAlive() || !IsSouthportEnemy(player))
                continue;

            // same legality checks the core applies before a guard answers a call for help
            if (!CombatManager::CanBeginCombat(me, player) || !me->IsValidAttackTarget(player)
                || !me->IsWithinLOSInMap(player))
                continue;

            me->EngageWithTarget(player);
            break;
        }
    }

    // hostile to Southport, flagged for PvP, or fighting any NPC of the archer's own faction
    // (Southport 2000 covers both the guard template 2237 and the townsfolk template 2238).
    // Cheapest checks first: most players in town clear the flag and empty combat list, so
    // the reputation lookup behind IsHostileTo runs last.
    bool IsSouthportEnemy(Player const* player) const
    {
        if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP))
            return true;

        if (FactionTemplateEntry const* myTemplate = me->GetFactionTemplateEntry())
            if (myTemplate->Faction)
                for (auto const& pair : player->GetCombatManager().GetPvECombatRefs())
                    if (FactionTemplateEntry const* otherTemplate = pair.second->GetOther(player)->GetFactionTemplateEntry())
                        if (otherTemplate->Faction == myTemplate->Faction)
                            return true;

        return me->IsHostileTo(player);
    }

    uint32 _shootTimer = 0;
    uint32 _watchTimer = 0;
};

void AddSC_southport_guard()
{
    RegisterCreatureAI(npc_southport_guard);
    RegisterCreatureAI(npc_southport_cannoneer);
    RegisterCreatureAI(npc_southport_archer);
}
