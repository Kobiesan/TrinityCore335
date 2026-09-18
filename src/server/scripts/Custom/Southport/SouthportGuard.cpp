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
#include "Creature.h"
#include "GameTime.h"
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
}

struct npc_southport_guard : public GuardAI
{
    npc_southport_guard(Creature* creature) : GuardAI(creature) { }

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
    uint32 _saluteReadyAt = 0;
};

void AddSC_southport_guard()
{
    RegisterCreatureAI(npc_southport_guard);
}
