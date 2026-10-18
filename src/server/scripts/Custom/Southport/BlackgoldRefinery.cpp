/* Southport city-lamp refill quests. */
#include "ScriptMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptedGossip.h"
#include "Item.h"
#include "CityLampJournal.h"
#include "SpellInfo.h"
#include <list>

// Each quest-provided oil can keeps its used spawn IDs in native item text.
// Item text and quest counts save in the player's normal transaction, so the
// journal survives logout without new character tables or shared lamp state.
class go_southport_city_lamp : public GameObjectScript
{
public:
    go_southport_city_lamp() : GameObjectScript("go_southport_city_lamp") { }

    struct LampAI : public GameObjectAI
    {
        explicit LampAI(GameObject* go) : GameObjectAI(go) { }
        static constexpr uint32 QuestId = 5000088;
        static constexpr uint32 DailyQuestId = 5000089;
        static constexpr uint32 OilCan = 5000436;
        static constexpr uint32 Lamp = 5000329;
        static constexpr uint32 Greeting = 5000830;
        static constexpr uint32 FilledGreeting = 5000831;
        static constexpr uint32 RefillSpell = 500181;

        uint32 ActiveQuest(Player const* player) const
        {
            for (uint32 questId : { QuestId, DailyQuestId })
                if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                    return questId;
            return 0;
        }

        Optional<bool> CanInteractWith(Player const* player) const override
        {
            std::vector<std::uint64_t> used;
            return CanFill(player, used) != nullptr;
        }

        bool IsFilled(Player const* player) const
        {
            Item* can = player->GetItemByEntry(OilCan);
            std::vector<std::uint64_t> used;
            if (!can || !SouthportLamps::Read(can->GetText(), used))
                return false;
            for (auto spawn : used)
                if (spawn == me->GetSpawnId())
                    return true;
            return false;
        }

        bool FinishRefill(Player* player)
        {
            std::vector<std::uint64_t> used;
            Item* can = CanFill(player, used);
            if (!can)
                return false;
            used.push_back(me->GetSpawnId());
            can->SetText(SouthportLamps::Write(used));
            can->SetState(ITEM_CHANGED, player);
            player->KillCreditGO(Lamp, me->GetGUID());
            player->UpdateVisibleGameobjectsOrSpellClicks();
            ChatHandler(player->GetSession()).SendSysMessage("You refill the lamp with oil.");
            ClearGossipMenuFor(player);
            SendGossipMenuFor(player, FilledGreeting, me->GetGUID());
            return true;
        }

        Item* CanFill(Player const* player, std::vector<std::uint64_t>& used) const
        {
            uint32 const questId = ActiveQuest(player);
            if (!player->IsAlive() || !questId
                || !me->GetSpawnId())
                return nullptr;
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest || quest->RequiredNpcOrGo[0] != -int32(Lamp))
                return nullptr;
            std::size_t required = quest->RequiredNpcOrGoCount[0];
            Item* can = player->GetItemByEntry(OilCan);
            if (!can || !SouthportLamps::Read(can->GetText(), used, required)
                || used.size() != player->GetReqKillOrCastCurrentCount(questId, -int32(Lamp))
                || used.size() >= required)
                return nullptr;
            for (auto spawn : used)
                if (spawn == me->GetSpawnId())
                    return nullptr;
            return can;
        }

        void SpellHit(WorldObject* caster, SpellInfo const* spell) override
        {
            if (spell->Id == RefillSpell)
                if (Player* player = caster->ToPlayer())
                    FinishRefill(player);
        }

        bool OnGossipHello(Player* player) override
        {
            ClearGossipMenuFor(player);
            std::vector<std::uint64_t> used;
            if (!CanFill(player, used))
            {
                CloseGossipMenuFor(player);
                return true;
            }
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Refill the lamp with oil.", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            SendGossipMenuFor(player, IsFilled(player) ? FilledGreeting : Greeting, me->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, uint32 /*menuId*/, uint32 listId) override
        {
            uint32 sender = player->PlayerTalkClass->GetGossipOptionSender(listId);
            uint32 action = player->PlayerTalkClass->GetGossipOptionAction(listId);
            ClearGossipMenuFor(player);
            CloseGossipMenuFor(player);
            std::vector<std::uint64_t> used;
            if (sender != GOSSIP_SENDER_MAIN || action != GOSSIP_ACTION_INFO_DEF + 1)
                return true;
            if (CanFill(player, used))
            {
                if (!player->IsNonMeleeSpellCast(false))
                    player->CastSpell(me, RefillSpell, false);
            }
            else
                ChatHandler(player->GetSession()).SendSysMessage("This lamp cannot be refilled right now.");
            // Handle the use completely: no activation, despawn, loot or global
            // lockout. Other players can refill the same post independently.
            return true;
        }
    };

    GameObjectAI* GetAI(GameObject* go) const override { return new LampAI(go); }
};

void AddSC_blackgold_refinery()
{
    new go_southport_city_lamp();
}
