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

/*
 * Southport: the riding skill grows as the player rides.
 *
 * Riding (skill 762) starts at value 1 when a player learns Apprentice Riding
 * from a trainer and is capped by the rank they have bought (Apprentice 75 /
 * Journeyman 150 / Expert 225 / Artisan 300). Mount ground speed is derived
 * from the skill in Unit::UpdateSpeed (-50% at skill 1, normal at 75, +50% at 300).
 *
 * This script samples mounted players on a timer: while mounted and actually
 * moving on the ground (not in water, flying, on a transport/vehicle, in a
 * battleground/arena, or teleporting) they accrue a fixed interval of travel
 * and then roll once per interval for +1 skill, with a chance that falls as
 * the skill rises. All checks are server-side; no client packets are sniffed.
 */

#include "ScriptMgr.h"
#include "Player.h"
#include "Unit.h"
#include "World.h"
#include "WorldSession.h"

#include <cmath>
#include <unordered_map>

namespace
{
    constexpr uint32 RIDING_TICK_MS = 250;       // how often players are sampled
    constexpr uint32 RIDING_GAIN_MS = 3000;      // travel time per skill-up attempt
    constexpr float  RIDING_MIN_DIST = 10.0f;    // minimum yards travelled per attempt

    struct RidingState
    {
        float x = 0.0f;
        float y = 0.0f;
        float dist = 0.0f;   // yards travelled in the current attempt window
        uint32 elapsedMs = 0;
        bool init = false;
    };

    std::unordered_map<ObjectGuid::LowType, RidingState> ridingStates;

    // Chance (out of 10000) of +1 skill per attempt, by current skill value.
    // Tuned so 1 -> 300 averages ~12.8h of continuous riding: roughly a couple
    // weeks at about an hour of riding a day. The last stretch dominates.
    uint32 RidingGainChance(uint32 skill)
    {
        if (skill <= 30)  return 500;   // 5.00%
        if (skill <= 75)  return 400;   // 4.00%
        if (skill <= 150) return 300;   // 3.00%
        if (skill <= 225) return 200;   // 2.00%
        return 100;                     // 1.00% up to 300
    }
}

class SouthportRidingWorldScript : public WorldScript
{
public:
    SouthportRidingWorldScript() : WorldScript("SouthportRidingWorldScript"), _acc(0) { }

    void OnUpdate(uint32 diff) override
    {
        _acc += diff;
        if (_acc < RIDING_TICK_MS)
            return;

        uint32 const dt = _acc;
        _acc = 0;

        for (auto const& pair : sWorld->GetAllSessions())
        {
            WorldSession* session = pair.second;
            Player* player = session ? session->GetPlayer() : nullptr;
            if (!player)
                continue;

            ObjectGuid::LowType const guid = player->GetGUID().GetCounter();

            if (!player->IsInWorld()
                || !player->IsMounted() || !player->HasSkill(SKILL_RIDING)
                || player->IsInWater() || player->IsFlying() || player->IsInFlight()
                || player->GetTransport() || player->GetVehicle()
                || player->IsBeingTeleported() || player->InBattleground() || player->InArena()
                || player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING))
            {
                ridingStates.erase(guid);
                continue;
            }

            RidingState& st = ridingStates[guid];
            float const px = player->GetPositionX();
            float const py = player->GetPositionY();

            if (!st.init)
            {
                st.x = px;
                st.y = py;
                st.init = true;
                continue;
            }

            float const dx = px - st.x;
            float const dy = py - st.y;
            st.x = px;
            st.y = py;
            st.dist += std::sqrt(dx * dx + dy * dy);

            // Only accrue while actually moving.
            if (!player->HasUnitMovementFlag(MOVEMENTFLAG_MASK_MOVING))
            {
                st.elapsedMs = 0;
                st.dist = 0.0f;
                continue;
            }

            st.elapsedMs += dt;
            if (st.elapsedMs < RIDING_GAIN_MS)
                continue;

            float const travelled = st.dist;
            st.elapsedMs = 0;
            st.dist = 0.0f;

            if (travelled < RIDING_MIN_DIST)
                continue;

            uint32 const skill = player->GetPureSkillValue(SKILL_RIDING);
            if (skill >= player->GetPureMaxSkillValue(SKILL_RIDING))
                continue;

            if (urand(1, 10000) <= RidingGainChance(skill))
                player->UpdateSkill(SKILL_RIDING, 1);   // refreshes mount speed via core
        }
    }

private:
    uint32 _acc;
};

class SouthportRidingPlayerScript : public PlayerScript
{
public:
    SouthportRidingPlayerScript() : PlayerScript("SouthportRidingPlayerScript") { }

    void OnLogout(Player* player) override
    {
        ridingStates.erase(player->GetGUID().GetCounter());
    }
};

void AddSC_southport_riding()
{
    new SouthportRidingWorldScript();
    new SouthportRidingPlayerScript();
}
