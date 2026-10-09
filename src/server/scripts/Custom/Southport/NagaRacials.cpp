/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information.
 * Licensed under the GNU General Public License, version 2 or later.
 */
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "Unit.h"

// 90178 - Fleet Footed. Refresh on learning, login, resurrection and removal.
// Unit::UpdateSpeed also keeps it synchronized with movement auras and riding.
class spell_tbv_naga_fleet_footed : public AuraScript
{
    PrepareAuraScript(spell_tbv_naga_fleet_footed);

    void Refresh(AuraEffect const*, AuraEffectHandleModes)
    {
        GetTarget()->UpdateSpeed(MOVE_RUN_BACK);
    }

    void Register() override
    {
        AfterEffectApply += AuraEffectApplyFn(spell_tbv_naga_fleet_footed::Refresh,
            EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
        AfterEffectRemove += AuraEffectRemoveFn(spell_tbv_naga_fleet_footed::Refresh,
            EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_southport_naga_racials()
{
    RegisterSpellScript(spell_tbv_naga_fleet_footed);
}
