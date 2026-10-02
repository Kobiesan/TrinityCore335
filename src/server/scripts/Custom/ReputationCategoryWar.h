#ifndef TBV_REPUTATION_CATEGORY_WAR_H
#define TBV_REPUTATION_CATEGORY_WAR_H

#include "ScriptMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "DBCStores.h"

// Stock client FactionToggleAtWar rejects category headers. A self-addressed
// addon request bridges only our two actionable parent factions to the same
// server-side reputation checks used by normal At War packets.
class ReputationCategoryWar : public PlayerScript
{
public:
    ReputationCategoryWar() : PlayerScript("ReputationCategoryWar") { }

    void OnChat(Player* player, uint32 type, uint32 lang, std::string& message, Player* receiver) override
    {
        if (receiver != player || type != CHAT_MSG_WHISPER || lang != uint32(LANG_ADDON))
            return;

        uint32 factionId = 0;
        bool enabled = false;
        if (message == "AORepWar\t21:1") { factionId = 21; enabled = true; }
        else if (message == "AORepWar\t21:0") factionId = 21;
        else if (message == "AORepWar\t169:1") { factionId = 169; enabled = true; }
        else if (message == "AORepWar\t169:0") factionId = 169;
        else return;

        if (FactionEntry const* faction = sFactionStore.LookupEntry(factionId))
            player->GetReputationMgr().SetAtWar(faction->ReputationIndex, enabled);
        message.clear();
    }
};

#endif
