/*
 * Southport: the two competing manifests. Turning in one to Dockmaster Tessa
 * Vane fails the other, so the player's choice actually matters.
 */

#include "Creature.h"
#include "CreatureAI.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"

namespace
{
    constexpr uint32 QUEST_TRUE_MANIFEST  = 5000007; // A Ship With No Name
    constexpr uint32 QUEST_FALSE_MANIFEST = 5000008; // Clean Papers

    // FailQuest only changes an INCOMPLETE quest. If the rival is already objective-
    // complete (the player holds both papers) knock it back so it can be failed, and
    // leave it failed in the quest log.
    void DropRival(Player* player, uint32 questId)
    {
        if (!player)
            return;

        QuestStatus status = player->GetQuestStatus(questId);
        if (status == QUEST_STATUS_NONE)
            return;

        if (status == QUEST_STATUS_COMPLETE)
            player->SetQuestStatus(questId, QUEST_STATUS_INCOMPLETE);

        player->FailQuest(questId);
    }
}

struct npc_tessa_vane : public CreatureAI
{
    npc_tessa_vane(Creature* creature) : CreatureAI(creature) { }

    void UpdateAI(uint32 /*diff*/) override { }

    void OnQuestReward(Player* player, Quest const* quest, uint32 /*opt*/) override
    {
        switch (quest->GetQuestId())
        {
            case QUEST_TRUE_MANIFEST:  DropRival(player, QUEST_FALSE_MANIFEST); break;
            case QUEST_FALSE_MANIFEST: DropRival(player, QUEST_TRUE_MANIFEST);  break;
            default: break;
        }
    }
};

void AddSC_southport_manifests()
{
    RegisterCreatureAI(npc_tessa_vane);
}
