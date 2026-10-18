#ifndef SOUTHPORT_CITY_LAMP_JOURNAL_H
#define SOUTHPORT_CITY_LAMP_JOURNAL_H

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace SouthportLamps
{
    constexpr std::string_view Prefix = "SouthportLampOil:1:";
    constexpr std::size_t MaximumEntries = 255;

    inline bool Read(std::string_view text, std::vector<std::uint64_t>& used, std::size_t limit = MaximumEntries)
    {
        used.clear();
        if (!limit || limit > MaximumEntries)
            return false;
        if (text.empty())
            return true;
        if (text.substr(0, Prefix.size()) != Prefix)
            return false;
        text.remove_prefix(Prefix.size());
        while (!text.empty())
        {
            auto end = text.find(',');
            auto token = text.substr(0, end);
            std::uint64_t value = 0;
            auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
            if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || !value || used.size() >= limit)
                return false;
            for (auto previous : used)
                if (previous == value)
                    return false;
            used.push_back(value);
            if (end == std::string_view::npos)
                break;
            text.remove_prefix(end + 1);
            if (text.empty())
                return false;
        }
        return true;
    }

    inline std::string Write(std::vector<std::uint64_t> const& used)
    {
        std::string text(Prefix);
        for (auto value : used)
        {
            if (text.size() != Prefix.size())
                text += ',';
            text += std::to_string(value);
        }
        return text;
    }
}
#endif
