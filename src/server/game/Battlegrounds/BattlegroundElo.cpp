#include "BattlegroundElo.h"
#include "Battleground.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "StringFormat.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr uint32 START_RATING = 1500;
    constexpr uint32 MIN_RATING = 500;
    constexpr uint32 MAX_RATING = 3500;
    constexpr uint32 MIN_PLAYERS_PER_TEAM = 2;
    constexpr uint32 MIN_MATCH_MS = 2 * MINUTE * IN_MILLISECONDS;
    constexpr int32 K_PROVISIONAL = 40;
    constexpr int32 K_ESTABLISHED = 32;
    constexpr double PERFORMANCE_WEIGHT = 8.0;

    struct RatingState
    {
        uint32 Rating = START_RATING;
        uint32 Games = 0;
        uint32 Wins = 0;
        uint32 Losses = 0;
        uint32 Draws = 0;
        uint32 Kills = 0;
        uint32 Deaths = 0;
        uint32 HonorableKills = 0;
        uint64 DamageDone = 0;
        uint64 HealingDone = 0;
        uint32 FlagCaptures = 0;
        uint32 FlagReturns = 0;
        uint32 BasesAssaulted = 0;
        uint32 BasesDefended = 0;
    };

    struct Participant
    {
        ObjectGuid Guid;
        uint32 GuidCounter = 0;
        uint32 Team = 0;
        BattlegroundScoreSnapshot Score;
        RatingState State;
        double RawPerformance = 0.0;
        double Performance = 0.0;
    };

    double Mean(std::vector<double> const& values)
    {
        if (values.empty())
            return 0.0;

        double sum = 0.0;
        for (double value : values)
            sum += value;
        return sum / double(values.size());
    }

    double StdDev(std::vector<double> const& values, double mean)
    {
        if (values.size() < 2)
            return 0.0;

        double sum = 0.0;
        for (double value : values)
            sum += (value - mean) * (value - mean);
        return std::sqrt(sum / double(values.size() - 1));
    }

    uint32 TeamIndex(uint32 team)
    {
        return team == ALLIANCE ? 0 : 1;
    }

    std::string BuildMatchKey(Battleground const* bg, uint32 winner)
    {
        return Trinity::StringFormat("{}-{}-{}-{}",
            uint32(bg->GetTypeID(true)), bg->GetInstanceID(),
            uint32(GameTime::GetGameTime()), winner);
    }

    void LoadRatings(uint32 bgType, std::vector<Participant>& participants)
    {
        if (participants.empty())
            return;

        std::string guidList;
        for (Participant const& participant : participants)
        {
            if (!guidList.empty())
                guidList += ",";
            guidList += std::to_string(participant.GuidCounter);
        }

        if (QueryResult result = CharacterDatabase.PQuery(
            "SELECT guid, rating, games, wins, losses, draws, kills, deaths, honorable_kills, "
            "damage_done, healing_done, flag_captures, flag_returns, bases_assaulted, bases_defended "
            "FROM character_battleground_elo WHERE battleground = {} AND guid IN ({})",
            bgType, guidList))
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guid = fields[0].GetUInt32();
                for (Participant& participant : participants)
                {
                    if (participant.GuidCounter != guid)
                        continue;

                    participant.State.Rating = fields[1].GetUInt32();
                    participant.State.Games = fields[2].GetUInt32();
                    participant.State.Wins = fields[3].GetUInt32();
                    participant.State.Losses = fields[4].GetUInt32();
                    participant.State.Draws = fields[5].GetUInt32();
                    participant.State.Kills = fields[6].GetUInt32();
                    participant.State.Deaths = fields[7].GetUInt32();
                    participant.State.HonorableKills = fields[8].GetUInt32();
                    participant.State.DamageDone = fields[9].GetUInt64();
                    participant.State.HealingDone = fields[10].GetUInt64();
                    participant.State.FlagCaptures = fields[11].GetUInt32();
                    participant.State.FlagReturns = fields[12].GetUInt32();
                    participant.State.BasesAssaulted = fields[13].GetUInt32();
                    participant.State.BasesDefended = fields[14].GetUInt32();
                    break;
                }
            } while (result->NextRow());
        }
    }

    void AddRawPerformance(Participant& participant, BattlegroundTypeId type)
    {
        double raw = 0.5 * participant.Score.KillingBlows
            + 0.25 * participant.Score.HonorableKills
            - 0.5 * participant.Score.Deaths
            + double(participant.Score.DamageDone) / 100000.0
            + double(participant.Score.HealingDone) / 100000.0;

        if (type == BATTLEGROUND_WS)
            raw += 2.0 * participant.Score.Attr1 + participant.Score.Attr2;
        else
            raw += 2.0 * participant.Score.Attr1 + 2.0 * participant.Score.Attr2;

        participant.RawPerformance = raw;
    }

    void ComputePerformance(std::vector<Participant*>& team, BattlegroundTypeId type)
    {
        std::vector<double> values;
        values.reserve(team.size());

        for (Participant* participant : team)
        {
            AddRawPerformance(*participant, type);
            values.push_back(participant->RawPerformance);
        }

        double mean = Mean(values);
        double deviation = std::max(1.0, StdDev(values, mean));
        for (Participant* participant : team)
            participant->Performance = std::clamp((participant->RawPerformance - mean) / deviation / 2.0, -1.0, 1.0);
    }
}

void BattlegroundElo::OnBattlegroundEnd(Battleground const* bg, uint32 winner)
{
    if (!bg || !bg->isBattleground())
        return;

    if (bg->GetStatus() != STATUS_IN_PROGRESS)
        return;

    BattlegroundTypeId type = bg->GetTypeID(true);
    if (type != BATTLEGROUND_WS && type != BATTLEGROUND_AB)
        return;

    uint32 elapsed = bg->GetElapsedTime();
    if (elapsed <= uint32(BG_START_DELAY_2M))
        return;

    uint32 duration = elapsed - uint32(BG_START_DELAY_2M);
    if (duration < MIN_MATCH_MS)
        return;

    static std::mutex processedMutex;
    static std::unordered_set<std::string> processedMatches;

    std::string matchKey = BuildMatchKey(bg, winner);
    {
        std::lock_guard<std::mutex> lock(processedMutex);
        if (!processedMatches.insert(matchKey).second)
            return;
    }

    std::vector<Participant> participants;
    for (auto const& [guid, battlegroundPlayer] : bg->GetPlayers())
    {
        if (battlegroundPlayer.OfflineRemoveTime != 0)
            continue;

        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (!player || player->IsGameMaster())
            continue;

        BattlegroundScoreSnapshot snapshot;
        if (!bg->GetPlayerScoreSnapshot(guid, snapshot))
            continue;

        uint64 activity = uint64(snapshot.KillingBlows) + snapshot.HonorableKills + snapshot.DamageDone
            + snapshot.HealingDone + snapshot.Attr1 + snapshot.Attr2;
        if (!activity)
            continue;

        Participant participant;
        participant.Guid = guid;
        participant.GuidCounter = uint32(guid.GetCounter());
        participant.Team = battlegroundPlayer.Team == ALLIANCE ? ALLIANCE : battlegroundPlayer.Team == HORDE ? HORDE : 0;
        if (!participant.Team)
            continue;
        participant.Score = snapshot;
        participants.push_back(participant);
    }

    std::vector<Participant*> teams[2];
    for (Participant& participant : participants)
        teams[TeamIndex(participant.Team)].push_back(&participant);

    if (teams[0].size() < MIN_PLAYERS_PER_TEAM || teams[1].size() < MIN_PLAYERS_PER_TEAM)
        return;

    LoadRatings(uint32(type), participants);
    ComputePerformance(teams[0], type);
    ComputePerformance(teams[1], type);

    double average[2] = { 0.0, 0.0 };
    for (uint8 team = 0; team < 2; ++team)
    {
        double sum = 0.0;
        for (Participant* participant : teams[team])
            sum += participant->State.Rating;
        average[team] = sum / double(teams[team].size());
    }

    for (uint8 team = 0; team < 2; ++team)
    {
        uint32 const other = team == 0 ? 1 : 0;
        for (Participant* participant : teams[team])
        {
            double actual = winner == 0 ? 0.5 : (winner == participant->Team ? 1.0 : 0.0);
            double expected = 1.0 / (1.0 + std::pow(10.0, (average[other] - double(participant->State.Rating)) / 400.0));
            int32 k = participant->State.Games < 10 ? K_PROVISIONAL : K_ESTABLISHED;
            double delta = double(k) * (actual - expected) + PERFORMANCE_WEIGHT * participant->Performance;

            if (actual == 1.0 && delta < 0.0)
                delta = 0.0;
            else if (actual == 0.0 && delta > 0.0)
                delta = 0.0;

            int32 newRating = std::clamp<int32>(int32(participant->State.Rating) + int32(std::lround(delta)), int32(MIN_RATING), int32(MAX_RATING));
            participant->State.Rating = uint32(newRating);
            ++participant->State.Games;
            if (actual == 1.0)
                ++participant->State.Wins;
            else if (actual == 0.0)
                ++participant->State.Losses;
            else
                ++participant->State.Draws;

            participant->State.Kills += participant->Score.KillingBlows;
            participant->State.Deaths += participant->Score.Deaths;
            participant->State.HonorableKills += participant->Score.HonorableKills;
            participant->State.DamageDone += participant->Score.DamageDone;
            participant->State.HealingDone += participant->Score.HealingDone;
            if (type == BATTLEGROUND_WS)
            {
                participant->State.FlagCaptures += participant->Score.Attr1;
                participant->State.FlagReturns += participant->Score.Attr2;
            }
            else
            {
                participant->State.BasesAssaulted += participant->Score.Attr1;
                participant->State.BasesDefended += participant->Score.Attr2;
            }

            CharacterDatabase.PExecute(
                "INSERT INTO character_battleground_elo "
                "(guid, battleground, rating, games, wins, losses, draws, kills, deaths, honorable_kills, "
                "damage_done, healing_done, flag_captures, flag_returns, bases_assaulted, bases_defended, last_match_key) "
                "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, '{}') "
                "ON DUPLICATE KEY UPDATE "
                "rating = VALUES(rating), games = VALUES(games), wins = VALUES(wins), losses = VALUES(losses), "
                "draws = VALUES(draws), kills = VALUES(kills), deaths = VALUES(deaths), honorable_kills = VALUES(honorable_kills), "
                "damage_done = VALUES(damage_done), healing_done = VALUES(healing_done), flag_captures = VALUES(flag_captures), "
                "flag_returns = VALUES(flag_returns), bases_assaulted = VALUES(bases_assaulted), bases_defended = VALUES(bases_defended), "
                "last_match_key = VALUES(last_match_key)",
                participant->GuidCounter, uint32(type), participant->State.Rating, participant->State.Games,
                participant->State.Wins, participant->State.Losses, participant->State.Draws,
                participant->State.Kills, participant->State.Deaths, participant->State.HonorableKills,
                participant->State.DamageDone, participant->State.HealingDone,
                participant->State.FlagCaptures, participant->State.FlagReturns,
                participant->State.BasesAssaulted, participant->State.BasesDefended, matchKey);
        }
    }

    TC_LOG_INFO("scripts", "BattlegroundElo: rated {} players for battleground {} (instance {}).",
        uint32(participants.size()), uint32(type), bg->GetInstanceID());
}
