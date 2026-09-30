/*
 * Southport: the eight statue plaques (gameobject_template 5000103-5000110,
 * page_text 50010-50017, see Southport/southport_statues.sql).
 *
 * Each plaque is a stock type 9 GAMEOBJECT_TYPE_TEXT. The client reads the
 * page text id from the gameobject data and, when a player uses the plaque,
 * asks the server for that text (WorldSession::HandleQueryPageText, which
 * serves ObjectMgr::_pageTextStore). There is no server-side per-click hook,
 * so the boards are refreshed here on a timer: we recompute each board and
 * rewrite its page_text row, then reload the page text cache. No addon and no
 * custom packet are involved.
 *
 * All eight boards come straight from stored data:
 *   gold            characters.money
 *   deeds           criteria 12698 (earn achievement points)
 *   kills           characters.totalKills
 *   time played     characters.totaltime
 *   highest level   characters.level (will become prestige + level)
 *   largest hit     criteria 5373  (Largest hit dealt, melee and spells)
 *   largest heal    criteria 5376  (Largest heal cast)
 *   patrons         characters.southport_patrons
 */

#include "ScriptMgr.h"
#include "Chat.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RBAC.h"
#include "StringFormat.h"
#include "World.h"
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32 PAGE_MERCHANT_TALLY = 50010;
    constexpr uint32 PAGE_DEED_ROLL      = 50011;
    constexpr uint32 PAGE_BLOODY_TALLY   = 50012;
    constexpr uint32 PAGE_LONG_WATCH     = 50013;
    constexpr uint32 PAGE_HIGH_WATER_MARK = 50014;
    constexpr uint32 PAGE_HAMMERS_LEDGER = 50015;
    constexpr uint32 PAGE_MERCY_LEDGER   = 50016;
    constexpr uint32 PAGE_PATRONS_ROLL   = 50017;

    constexpr uint32 CRIT_ACHIEVEMENT_POINTS = 12698; // earn achievement points
    constexpr uint32 CRIT_HIGHEST_HIT        = 5373;  // largest hit dealt
    constexpr uint32 CRIT_HIGHEST_HEAL       = 5376;  // largest heal cast

    constexpr uint32 TOP_COUNT           = 5;
    constexpr uint32 REFRESH_INTERVAL_MS = 5 * 60 * 1000;

    using BoardRow = std::pair<std::string, std::string>;

    std::string WithCommas(uint64 value)
    {
        std::string text = std::to_string(value);
        for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3)
            text.insert(static_cast<size_t>(i), ",");
        return text;
    }

    std::string FormatMoney(uint64 copper)
    {
        uint64 gold = copper / 10000;
        uint64 silver = (copper % 10000) / 100;
        uint64 copperOnly = copper % 100;
        if (gold)
            return Trinity::StringFormat("{}g {}s {}c", WithCommas(gold), silver, copperOnly);
        if (silver)
            return Trinity::StringFormat("{}s {}c", silver, copperOnly);
        return Trinity::StringFormat("{}c", copperOnly);
    }

    // Account ids with any GM security level. Empty when none are configured.
    std::string GmAccountList()
    {
        std::string list;
        if (QueryResult result = LoginDatabase.Query(
            "SELECT DISTINCT AccountID FROM account_access WHERE SecurityLevel > 0"))
        {
            do
            {
                if (!list.empty())
                    list += ",";
                list += std::to_string(result->Fetch()[0].GetUInt32());
            } while (result->NextRow());
        }
        return list;
    }

    std::string FormatDuration(uint64 seconds)
    {
        uint64 days = seconds / 86400;
        uint64 hours = (seconds % 86400) / 3600;
        uint64 minutes = (seconds % 3600) / 60;
        if (days)
            return Trinity::StringFormat("{}d {}h", days, hours);
        if (hours)
            return Trinity::StringFormat("{}h {}m", hours, minutes);
        return Trinity::StringFormat("{}m", minutes);
    }

    std::string BuildBoard(std::string const& title, std::string const& description, std::vector<BoardRow> const& rows)
    {
        std::string text = title + "\n" + description + "\n\n";
        if (rows.empty())
            text += "No names yet.\n";
        else
        {
            uint32 rank = 1;
            for (BoardRow const& row : rows)
            {
                if (row.second.empty())
                    text += Trinity::StringFormat("{}. {}\n", rank++, row.first);
                else
                    text += Trinity::StringFormat("{}. {} - {}\n", rank++, row.first, row.second);
            }
        }
        return text;
    }

    void Publish(uint32 pageId, std::string const& text)
    {
        std::string escaped = text;
        WorldDatabase.EscapeString(escaped);
        WorldDatabase.DirectPExecute("UPDATE page_text SET Text = '{}' WHERE ID = {}", escaped, pageId);
    }

    std::vector<BoardRow> ReadProgressBoard(uint32 criteria, std::string const& accountFilter)
    {
        std::vector<BoardRow> rows;
        if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT c.name, p.counter FROM character_achievement_progress p "
            "JOIN characters c ON c.guid = p.guid "
            "WHERE p.criteria = {} AND p.counter > 0{} ORDER BY p.counter DESC LIMIT {}",
            criteria, accountFilter, TOP_COUNT))
        {
            do
            {
                Field* fields = result->Fetch();
                rows.emplace_back(fields[0].GetString(), WithCommas(fields[1].GetUInt64()));
            } while (result->NextRow());
        }
        return rows;
    }

    void RefreshLeaderboards()
    {
        std::string const gmAccounts = GmAccountList();
        std::string const charFilter = gmAccounts.empty() ? "" : (" AND account NOT IN (" + gmAccounts + ")");
        std::string const joinFilter = gmAccounts.empty() ? "" : (" AND c.account NOT IN (" + gmAccounts + ")");
        std::string const patronFilter = gmAccounts.empty() ? "" : (" AND (p.guid IS NULL OR c.account NOT IN (" + gmAccounts + "))");

        // Most gold
        {
            std::vector<BoardRow> rows;
            if (QueryResult result = CharacterDatabase.PQuery(
                "SELECT name, money FROM characters WHERE money > 0{} ORDER BY money DESC LIMIT {}", charFilter, TOP_COUNT))
            {
                do
                {
                    Field* fields = result->Fetch();
                    rows.emplace_back(fields[0].GetString(), FormatMoney(fields[1].GetUInt64()));
                } while (result->NextRow());
            }
            Publish(PAGE_MERCHANT_TALLY, BuildBoard("WALL OF FORTUNE", "Total gold held.", rows));
        }

        // Achievement points
        Publish(PAGE_DEED_ROLL, BuildBoard("WALL OF DEEDS", "Most achievement points earned.", ReadProgressBoard(CRIT_ACHIEVEMENT_POINTS, joinFilter)));

        // PvP kills
        {
            std::vector<BoardRow> rows;
            if (QueryResult result = CharacterDatabase.PQuery(
                "SELECT name, totalKills FROM characters WHERE totalKills > 0{} ORDER BY totalKills DESC LIMIT {}", charFilter, TOP_COUNT))
            {
                do
                {
                    Field* fields = result->Fetch();
                    rows.emplace_back(fields[0].GetString(), WithCommas(fields[1].GetUInt64()));
                } while (result->NextRow());
            }
            Publish(PAGE_BLOODY_TALLY, BuildBoard("WALL OF THE FALLEN", "Most player kills.", rows));
        }

        // Time played
        {
            std::vector<BoardRow> rows;
            if (QueryResult result = CharacterDatabase.PQuery(
                "SELECT name, totaltime FROM characters WHERE totaltime > 0{} ORDER BY totaltime DESC LIMIT {}", charFilter, TOP_COUNT))
            {
                do
                {
                    Field* fields = result->Fetch();
                    rows.emplace_back(fields[0].GetString(), FormatDuration(fields[1].GetUInt64()));
                } while (result->NextRow());
            }
            Publish(PAGE_LONG_WATCH, BuildBoard("WALL OF ENDURANCE", "Total time played.", rows));
        }

        // The Prestige Roll: highest Prestige rank, then highest level. A rank-1
        // character who has reset to level 1 still outranks a rank-0 level 20.
        {
            std::vector<BoardRow> rows;
            if (QueryResult result = CharacterDatabase.PQuery(
                "SELECT c.name, c.level, COALESCE(p.`rank`, 0) AS prank FROM characters c "
                "LEFT JOIN prestige p ON p.guid = c.guid "
                "WHERE (c.level > 1 OR COALESCE(p.`rank`, 0) > 0){} "
                "ORDER BY prank DESC, c.level DESC, c.name LIMIT {}", charFilter, TOP_COUNT))
            {
                do
                {
                    Field* fields = result->Fetch();
                    uint32 const prank = fields[2].GetUInt32();
                    std::string const mark = "Prestige " + std::to_string(prank)
                        + " - Level " + std::to_string(fields[1].GetUInt32());
                    rows.emplace_back(fields[0].GetString(), mark);
                } while (result->NextRow());
            }
            Publish(PAGE_HIGH_WATER_MARK, BuildBoard("WALL OF REBIRTH", "Highest Prestige rank, then level.", rows));
        }

        // Largest hit
        Publish(PAGE_HAMMERS_LEDGER, BuildBoard("WALL OF THE HAMMER", "Highest damage dealt in a single hit or spell.", ReadProgressBoard(CRIT_HIGHEST_HIT, joinFilter)));

        // Largest heal
        Publish(PAGE_MERCY_LEDGER, BuildBoard("WALL OF MERCY", "Highest healing done in a single cast.", ReadProgressBoard(CRIT_HIGHEST_HEAL, joinFilter)));

        // Patrons (donors), in-universe. `guid` links the row to the character,
        // so the live name is shown; `name` is only a fallback snapshot.
        {
            std::vector<BoardRow> rows;
            if (QueryResult result = CharacterDatabase.PQuery(
                "SELECT COALESCE(c.name, p.name) FROM southport_patrons p "
                "LEFT JOIN characters c ON c.guid = p.guid WHERE 1=1{} "
                "ORDER BY p.amount DESC, 1 LIMIT {}", patronFilter, TOP_COUNT))
            {
                do
                {
                    Field* fields = result->Fetch();
                    rows.emplace_back(fields[0].GetString(), std::string());
                } while (result->NextRow());
            }
            Publish(PAGE_PATRONS_ROLL, BuildBoard("WALL OF PATRONS", "Those who keep the harbour afloat.", rows));
        }

        sObjectMgr->LoadPageTexts();
        TC_LOG_INFO("scripts", "Southport: statue leaderboards refreshed.");
    }
}

class SouthportStatueWorldScript : public WorldScript
{
public:
    SouthportStatueWorldScript() : WorldScript("SouthportStatueWorldScript"), _timer(REFRESH_INTERVAL_MS) { }

    void OnStartup() override
    {
        RefreshLeaderboards();
    }

    void OnUpdate(uint32 diff) override
    {
        if (_timer <= diff)
        {
            _timer = REFRESH_INTERVAL_MS;
            RefreshLeaderboards();
        }
        else
            _timer -= diff;
    }

private:
    uint32 _timer;
};

class SouthportPatronCommandScript : public CommandScript
{
public:
    SouthportPatronCommandScript() : CommandScript("SouthportPatronCommandScript") { }

    Trinity::ChatCommands::ChatCommandTable GetCommands() const override
    {
        // Donor management is administrative; reuse the server-info permission
        // rather than add a new RBAC entry (same choice as the poker command).
        static Trinity::ChatCommands::ChatCommandTable commandTable =
        {
            { "patron add",    HandlePatronAdd,    rbac::RBAC_PERM_COMMAND_SERVER_INFO, Trinity::ChatCommands::Console::No },
            { "patron remove", HandlePatronRemove, rbac::RBAC_PERM_COMMAND_SERVER_INFO, Trinity::ChatCommands::Console::No },
            { "patron list",   HandlePatronList,   rbac::RBAC_PERM_COMMAND_SERVER_INFO, Trinity::ChatCommands::Console::No }
        };
        return commandTable;
    }

private:
    static bool ResolveCharacter(char const* name, uint32& guid, std::string& outName)
    {
        std::string escaped = name;
        CharacterDatabase.EscapeString(escaped);
        if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT guid, name FROM characters WHERE name = '{}'", escaped))
        {
            Field* fields = result->Fetch();
            guid = fields[0].GetUInt32();
            outName = fields[1].GetString();
            return true;
        }
        return false;
    }

    static bool HandlePatronAdd(ChatHandler* handler, char const* args)
    {
        std::string buffer = args ? args : "";
        char* cursor = buffer.data();

        char* name = strtok(cursor, " ");
        if (!name)
        {
            handler->SendSysMessage("Usage: .patron add <name-or-player> [amount]");
            handler->SetSentErrorMessage(true);
            return false;
        }

        char* amountArg = strtok(nullptr, " ");
        uint32 amount = amountArg ? uint32(atoi(amountArg)) : 0;

        // If the name is a character, link the guid so the board follows renames.
        // Otherwise a plain name is fine; guid stays NULL.
        uint32 guid = 0;
        std::string charName;
        if (ResolveCharacter(name, guid, charName))
        {
            std::string escapedName = charName;
            CharacterDatabase.EscapeString(escapedName);
            CharacterDatabase.DirectPExecute(
                "INSERT INTO southport_patrons (guid, name, amount) VALUES ({}, '{}', {}) "
                "ON DUPLICATE KEY UPDATE name = '{}', amount = {}",
                guid, escapedName, amount, escapedName, amount);
            handler->PSendSysMessage("{} is on the patrons' roll.", charName);
        }
        else
        {
            std::string plainName = name;
            CharacterDatabase.EscapeString(plainName);
            CharacterDatabase.DirectPExecute(
                "INSERT INTO southport_patrons (guid, name, amount) VALUES (NULL, '{}', {})",
                plainName, amount);
            handler->PSendSysMessage("{} is on the patrons' roll.", std::string(name));
        }
        return true;
    }

    static bool HandlePatronRemove(ChatHandler* handler, char const* args)
    {
        std::string buffer = args ? args : "";
        char* name = strtok(buffer.data(), " ");
        if (!name)
        {
            handler->SendSysMessage("Usage: .patron remove <name-or-player>");
            handler->SetSentErrorMessage(true);
            return false;
        }

        std::string escapedName = name;
        CharacterDatabase.EscapeString(escapedName);

        uint32 guid = 0;
        std::string charName;
        if (ResolveCharacter(name, guid, charName))
            CharacterDatabase.DirectPExecute(
                "DELETE FROM southport_patrons WHERE guid = {} OR name = '{}'", guid, escapedName);
        else
            CharacterDatabase.DirectPExecute(
                "DELETE FROM southport_patrons WHERE name = '{}'", escapedName);

        handler->PSendSysMessage("{} struck from the patrons' roll.", std::string(name));
        return true;
    }

    static bool HandlePatronList(ChatHandler* handler)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT p.id, p.guid, COALESCE(c.name, p.name), p.amount FROM southport_patrons p "
            "LEFT JOIN characters c ON c.guid = p.guid ORDER BY p.amount DESC, 3");

        if (!result)
        {
            handler->SendSysMessage("The patrons' roll is empty.");
            return true;
        }

        handler->SendSysMessage("Patrons' roll:");
        do
        {
            Field* fields = result->Fetch();
            handler->PSendSysMessage("  #{} guid {} - {} ({})",
                fields[0].GetUInt32(), fields[1].GetUInt32(), fields[2].GetString(), fields[3].GetUInt32());
        } while (result->NextRow());

        return true;
    }
};

void AddSC_southport_statues()
{
    new SouthportStatueWorldScript();
    new SouthportPatronCommandScript();
}
