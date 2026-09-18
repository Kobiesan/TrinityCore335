#ifndef TRINITY_POKER_ACHIEVEMENTS_H
#define TRINITY_POKER_ACHIEVEMENTS_H

#include <array>
#include <cstdint>

namespace PokerAchievements
{
    constexpr uint32_t FirstCriteria = 51000;
    constexpr uint32_t LastCriteria = 51021;
    constexpr uint32_t StreakCriteria = 51019;
    constexpr std::array<uint32_t, 22> Limits = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 10, 25, 50, 100, 250, 500, 1000, 1, 3, 1, 1 };

    constexpr bool IsPokerCriteria(uint32_t id)
    {
        return id >= FirstCriteria && id <= LastCriteria;
    }

    constexpr bool Advances(uint32_t id, bool won, uint32_t flags)
    {
        if (id >= 51010 && id <= 51017)
            return won;
        if (id == StreakCriteria)
            return won;
        uint32_t bit = id < 51010 ? id - FirstCriteria : id == 51018 ? 10 : id == 51020 ? 11 : 12;
        return (flags & (1u << bit)) != 0;
    }

    // Statistics: never-completing counters. Their Achievement.dbc entries carry
    // ACHIEVEMENT_FLAG_COUNTER (0x1) and their criteria have Quantity 0, so the
    // core never completes them and only the stored counter is displayed.
    constexpr uint32_t FirstStatCriteria = 51042;
    constexpr uint32_t LastStatCriteria = 51051;

    enum StatIndex : uint32_t
    {
        STAT_HANDS_PLAYED = 0,
        STAT_HANDS_WON,
        STAT_HANDS_LOST,
        STAT_HANDS_FOLDED,
        STAT_WIN_PERCENT,
        STAT_MOST_GOLD_WON,
        STAT_AMOUNT_WON,
        STAT_AMOUNT_LOST,
        STAT_LONGEST_STREAK,
        STAT_RAKE_PAID,
        STAT_COUNT
    };

    constexpr uint32_t StatCriteria(StatIndex stat) { return FirstStatCriteria + static_cast<uint32_t>(stat); }

    constexpr bool IsStatCriteria(uint32_t id)
    {
        return id >= FirstStatCriteria && id <= LastStatCriteria;
    }
}

#endif
