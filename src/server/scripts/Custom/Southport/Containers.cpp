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
 * Southport containers: items that roll loot from item_loot_template.
 *
 * The Dockmaster's Strongbox (5000226) has a deliberately tiny epic tier
 * (0.5%). A 1 in 200 pull is only worth having if the server notices it, so
 * an epic coming out of the box is broadcast.
 */

#include "Item.h"
#include "Loot.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"

#include <sstream>

namespace
{
    constexpr uint32 DOCKMASTERS_STRONGBOX = 5000226;
}

class SouthportContainerPlayerScript : public PlayerScript
{
public:
    SouthportContainerPlayerScript() : PlayerScript("SouthportContainerPlayerScript") { }

    void OnPlayerLootItem(Player* player, Item* item, Loot* loot) override
    {
        if (!player || !item || !loot || !loot->containerID)
            return;

        if (item->GetTemplate()->GetQuality() != ITEM_QUALITY_EPIC)
            return;

        Item* container = player->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(loot->containerID));
        if (!container || container->GetEntry() != DOCKMASTERS_STRONGBOX)
            return;

        // Build a real chat item link so it is clickable: |c<color>|Hitem:<fields>|h[name]|h|r
        uint32 const randomProperty = uint32(item->GetItemRandomPropertyId());
        uint32 const suffixFactor = item->GetItemSuffixFactor();

        std::ostringstream link;
        link << "|cffa335ee|Hitem:" << item->GetEntry()
            << ':' << item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT)
            << ":0:0:0:0"
            << ':' << randomProperty
            << ':' << suffixFactor
            << ":0:0|h[" << item->GetTemplate()->Name1 << "]|h|r";

        std::string message = player->GetName() + " pulled " + link.str()
            + " from a Dockmaster's Strongbox!";
        sWorld->SendGlobalText(message.c_str(), nullptr);
    }
};

void AddSC_southport_containers()
{
    new SouthportContainerPlayerScript();
}
