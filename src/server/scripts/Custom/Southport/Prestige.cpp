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
 * Southport: the prestige system.
 *
 * Every character starts at level 1 and the cap is 20. The Keeper of the Cycle
 * (creature_template 5000093, world_tbv/southport_prestige.sql) lets a level-20
 * character "prestige": it is reset to level 1 and its rank increases by one.
 *
 * Design notes (what a reset does, and why):
 *   * Quests are NOT cleared. The 1-20 Southport quests each award a one-time
 *     item, so replaying them would let players duplicate unique rewards / farm
 *     gold. Ranks and the Mark of Prestige are the reward loop instead.
 *   * Class trainer abilities are unlearned and their gold cost is refunded, so
 *     re-levelling re-trains them at no net cost (a deliberate re-progression
 *     beat, and a gold-neutral one).
 *   * Equipped gear above RequiredLevel 1 cannot be used at level 1, so it is
 *     detached and mailed back. Bag/bank items are left alone - they are not
 *     usable at level 1 anyway and the player will re-level.
 *   * Reputations and profession skill are kept.
 *   * Each prestige grants 1x Mark of Prestige (item 5000385), a tradeable
 *     token, spent later at a prestige quartermaster. Ranks are unlimited.
 *   * Milestone achievements (Achievement.dbc 5033-5038, category 900010)
 *     complete at ranks 1/5/10/25/50/100; achievement 5039 "Total Prestiges"
 *     is an external counter criterion (51052) set to the rank.
 *
 * The rank is persisted before the character is touched, so a failure can never
 * reset a player without recording the rank. The rank is cached per session;
 * rankCache is swept on logout.
 */

#include "ScriptMgr.h"
#include "AchievementMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "Trainer.h"
#include "WorldSession.h"

#include <ctime>
#include <string>
#include <unordered_map>

namespace
{
    constexpr uint32 NPC_PRESTIGE_KEEPER = 5000093;
    constexpr uint32 NPC_TEXT_MAIN     = 65100;
    constexpr uint32 NPC_TEXT_INFO     = 65101;

    constexpr uint32 ITEM_MARK_OF_PRESTIGE = 5000385;
    constexpr uint32 MAX_LEVEL         = 20;

    // Prestige achievement set (client+server Achievement.dbc, category 900010).
    constexpr uint32 CRIT_TOTAL_PRESTIGE = 51052;   // external counter criterion

    struct PrestigeMilestone
    {
        uint32 Rank;         // prestige rank required
        uint32 Achievement;  // Achievement.dbc id
        uint32 Title;        // CharTitles.dbc id awarded at this rank
    };

    constexpr PrestigeMilestone PRESTIGE_MILESTONES[] =
    {
        {  1, 5033, 179 },   // Prestige 1   - the Prestigious
        {  5, 5034, 180 },   // Prestige 5   - the Reborn
        { 10, 5035, 181 },   // Prestige 10  - the Unbroken
        { 25, 5036, 182 },   // Prestige 25  - the Timeless
        { 50, 5037, 183 },   // Prestige 50  - the Eternal
        {100, 5038, 184 },   // Prestige 100 - the Infinite
    };

    enum PrestigeAction : uint32
    {
        ACTION_MAIN     = 0,   // rebuild the main menu
        ACTION_INFO     = 1,
        ACTION_PRESTIGE = 2,
        ACTION_VENDOR   = 3
    };

    // guid -> rank, kept for the session so gossip/talent reads do not hit the DB
    std::unordered_map<ObjectGuid::LowType, uint32> rankCache;

    uint32 GetRank(Player* player)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        auto itr = rankCache.find(guid);
        if (itr != rankCache.end())
            return itr->second;

        uint32 rank = 0;
        std::string const query = "SELECT `rank` FROM prestige WHERE guid = " + std::to_string(guid);
        if (QueryResult result = CharacterDatabase.Query(query.c_str()))
            rank = result->Fetch()[0].GetUInt32();

        rankCache[guid] = rank;
        return rank;
    }

    void SetRank(Player* player, uint32 rank)
    {
        ObjectGuid::LowType guid = player->GetGUID().GetCounter();
        std::string const rankStr = std::to_string(rank);
        std::string const query =
            "INSERT INTO prestige (guid, `rank`) VALUES (" + std::to_string(guid) + ", " + rankStr + ")"
            " ON DUPLICATE KEY UPDATE `rank` = " + rankStr;
        CharacterDatabase.Execute(query.c_str());
        rankCache[guid] = rank;
    }

    // Grant the title for every milestone the rank has reached.
    void GrantPrestigeTitles(Player* player, uint32 rank)
    {
        for (PrestigeMilestone const& m : PRESTIGE_MILESTONES)
        {
            if (rank < m.Rank)
                break;
            if (CharTitlesEntry const* title = sCharTitlesStore.LookupEntry(m.Title))
                if (!player->HasTitle(title))
                    player->SetTitle(title);
        }
    }

    void SendMainMenu(Player* player, Creature* creature)
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "What is Prestige?",
            GOSSIP_SENDER_MAIN, ACTION_INFO);
        // Show the rank inline; clicking it just rebuilds this menu.
        AddGossipItemFor(player, GOSSIP_ICON_DOT,
            "Prestige Rank: " + std::to_string(GetRank(player)),
            GOSSIP_SENDER_MAIN, ACTION_MAIN);
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "Browse your wares",
            GOSSIP_SENDER_MAIN, ACTION_VENDOR);

        if (player->GetLevel() == MAX_LEVEL)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1,
                "Prestige now. (Rank " + std::to_string(GetRank(player)) + " -> "
                    + std::to_string(GetRank(player) + 1) + ")",
                GOSSIP_SENDER_MAIN, ACTION_PRESTIGE,
                "Reset to level 1 and gain 1 Prestige rank and 1 Mark of Prestige?\n\n"
                "You keep quests, reputations, professions and all Prestige ranks. "
                "You lose your level, talent points, and the class abilities you bought from trainers "
                "(the gold is refunded). Equipped items that require a level above 1 are mailed to you. "
                "This cannot be undone.", 0, false);

        SendGossipMenuFor(player, NPC_TEXT_MAIN, creature);
    }

    void SendInfoMenu(Player* player, Creature* creature)
    {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Back...", GOSSIP_SENDER_MAIN, ACTION_MAIN);
        SendGossipMenuFor(player, NPC_TEXT_INFO, creature);
    }

    void MailToken(Player* player, uint32 entry, uint32 count, std::string const& subject, std::string const& body)
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (Item* item = Item::CreateItem(entry, count, player))
        {
            item->SaveToDB(trans);
            MailDraft(subject, body)
                .AddItem(item)
                .SendMailTo(trans, MailReceiver(player), MailSender(MAIL_CREATURE, NPC_PRESTIGE_KEEPER), MAIL_CHECK_MASK_NOT_RETURNABLE);
        }
        CharacterDatabase.CommitTransaction(trans);
    }

    // Unlearn every class trainer ability the player knows and refund what it cost.
    void RefundTrainerSpells(Player* player)
    {
        for (Trainer::Trainer const* trainer : sObjectMgr->GetClassTrainers(player->GetClass()))
        {
            for (Trainer::Spell const& spell : trainer->GetSpells())
            {
                if (!player->HasSpell(spell.SpellId))
                    continue;

                if (spell.MoneyCost)
                    player->ModifyMoney(static_cast<int32>(spell.MoneyCost));

                // learn_low_rank=false: do not fall back to a lower rank of the ability
                player->RemoveSpell(spell.SpellId, false, false);
            }
        }
    }

    // Detach equipped items above level 1 and mail them back, so the level-1
    // character cannot keep wearing gear it cannot equip.
    void ReturnEquippedGear(Player* player)
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item || item->GetTemplate()->GetRequiredLevel() <= 1)
                continue;

            player->MoveItemFromInventory(INVENTORY_SLOT_BAG_0, slot, true);
            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);

            MailDraft("Prestige Return", "An item you had equipped when you prestiged. It is yours again.")
                .AddItem(item)
                .SendMailTo(trans, MailReceiver(player), MailSender(MAIL_CREATURE, NPC_PRESTIGE_KEEPER), MAIL_CHECK_MASK_NOT_RETURNABLE);
        }
        CharacterDatabase.CommitTransaction(trans);
    }

    // Complete any prestige milestones the rank has passed and refresh the
    // "Total Prestiges" counter. Safe to call repeatedly (idempotent).
    void SyncPrestigeAchievements(Player* player, uint32 rank)
    {
        for (PrestigeMilestone const& m : PRESTIGE_MILESTONES)
        {
            if (rank < m.Rank)
                break;
            if (player->GetAchievementMgr()->HasAchieved(m.Achievement))
                continue;
            if (AchievementEntry const* entry = sAchievementStore.LookupEntry(m.Achievement))
                player->CompletedAchievement(entry);
        }

        player->GetAchievementMgr()->ApplyExternalCriteriaProgress(CRIT_TOTAL_PRESTIGE, rank, time(nullptr));
    }

    void DoPrestige(Player* player)
    {
        if (player->GetLevel() != MAX_LEVEL)
        {
            CloseGossipMenuFor(player);
            ChatHandler(player->GetSession()).SendSysMessage("You must be level 20 to prestige.");
            return;
        }

        uint32 const rank = GetRank(player) + 1;
        SetRank(player, rank);          // record the rank before touching the character
        GrantPrestigeTitles(player, rank);

        ReturnEquippedGear(player);
        RefundTrainerSpells(player);
        player->ResetTalents();
        player->SetLevel(1);

        if (!player->AddItem(ITEM_MARK_OF_PRESTIGE, 1))
            MailToken(player, ITEM_MARK_OF_PRESTIGE, 1, "Mark of Prestige",
                "Your prestige reward would not fit in your bags, so here it is by post.");

        SyncPrestigeAchievements(player, rank);

        CloseGossipMenuFor(player);

        ChatHandler handler(player->GetSession());
        handler.SendSysMessage("Prestige rank " + std::to_string(rank) + ". You received a Mark of Prestige.");
        handler.SendSysMessage("Equipped items above level 1 have been mailed back to you.");
    }
}

class npc_prestige_keeper : public CreatureScript
{
public:
    npc_prestige_keeper() : CreatureScript("npc_prestige_keeper") { }

    struct npc_prestige_keeperAI : public ScriptedAI
    {
        npc_prestige_keeperAI(Creature* creature) : ScriptedAI(creature) { }

        bool OnGossipHello(Player* player) override
        {
            SendMainMenu(player, me);
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 gossipListId) override
        {
            uint32 const action = player->PlayerTalkClass->GetGossipOptionAction(gossipListId);
            ClearGossipMenuFor(player);

            switch (action)
            {
                case ACTION_MAIN:
                    SendMainMenu(player, me);
                    break;
                case ACTION_INFO:
                    SendInfoMenu(player, me);
                    break;
                case ACTION_PRESTIGE:
                    DoPrestige(player);
                    break;
                case ACTION_VENDOR:
                    CloseGossipMenuFor(player);
                    player->GetSession()->SendListInventory(me->GetGUID());
                    break;
                default:
                    CloseGossipMenuFor(player);
                    break;
            }
            return true;
        }
    };

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new npc_prestige_keeperAI(creature);
    }
};

// Sweep the session cache on logout.
class PrestigePlayerScript : public PlayerScript
{
public:
    PrestigePlayerScript() : PlayerScript("PrestigePlayerScript") { }

    void OnLogin(Player* player, bool /*firstLogin*/) override
    {
        uint32 const rank = GetRank(player);
        if (rank >= 1)
        {
            GrantPrestigeTitles(player, rank);
            SyncPrestigeAchievements(player, rank);
        }
    }

    void OnLogout(Player* player) override
    {
        rankCache.erase(player->GetGUID().GetCounter());
    }
};

void AddSC_southport_prestige()
{
    // Register the prestige counter criterion as external before any player can
    // load, so ordinary game events never advance it (mirrors the poker range).
    AchievementMgr::RegisterExternalCriteriaRange(CRIT_TOTAL_PRESTIGE, CRIT_TOTAL_PRESTIGE);

    new npc_prestige_keeper();
    new PrestigePlayerScript();
}
