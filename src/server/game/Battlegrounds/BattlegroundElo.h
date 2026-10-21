#ifndef TRINITY_BATTLEGROUND_ELO_H
#define TRINITY_BATTLEGROUND_ELO_H

#include "Define.h"

class Battleground;

namespace BattlegroundElo
{
    void OnBattlegroundEnd(Battleground const* bg, uint32 winner);
}

#endif
