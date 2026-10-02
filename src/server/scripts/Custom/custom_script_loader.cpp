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

#include "Poker/Poker.h"
#include "HighElfMeditation.h"
#include "ReputationCategoryWar.h"
#include "Southport/SouthportGuard.h"
#include "Southport/StatueLeaderboards.h"

// This is where scripts' loading functions should be declared:
void AddSC_GOMove_commandscript();
void AddSC_southport_containers();
void AddSC_southport_manifests();
void AddSC_southport_prestige();
void AddSC_southport_riding();

// The name of this function should match:
// void Add${NameOfDirectory}Scripts()
void AddCustomScripts()
{
    RegisterSpellScript(spell_tbv_queldorei_meditation);
    new ReputationCategoryWar();
    AddSC_GOMove_commandscript();
    AddSC_poker();
    AddSC_southport_containers();
    AddSC_southport_guard();
    AddSC_southport_manifests();
    AddSC_southport_prestige();
    AddSC_southport_riding();
    AddSC_southport_statues();
}
