/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information.
 * Licensed under the GNU General Public License, version 2 or later.
 */

#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellScript.h"
#include "Unit.h"

namespace
{
    constexpr uint32 SPELL_GOBLIN_WINDED = 500132;
}

// 500130 - Exit Strategy: the DBC applies +40% speed and pacify/silence for 5s.
// Turtle's follow-up penalty starts only when the sprint naturally expires.
class spell_tbv_goblin_exit_strategy : public AuraScript
{
    PrepareAuraScript(spell_tbv_goblin_exit_strategy);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_GOBLIN_WINDED });
    }

    void AfterRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        if (GetTargetApplication()->GetRemoveMode() != AURA_REMOVE_BY_EXPIRE)
            return;

        Unit* target = GetTarget();
        if (target->IsAlive())
            target->CastSpell(target, SPELL_GOBLIN_WINDED, true);
    }

    void Register() override
    {
        AfterEffectRemove += AuraEffectRemoveFn(spell_tbv_goblin_exit_strategy::AfterRemove,
            EFFECT_0, SPELL_AURA_MOD_INCREASE_SPEED, AURA_EFFECT_HANDLE_REAL);
    }
};

void AddSC_southport_goblin_racials()
{
    RegisterSpellScript(spell_tbv_goblin_exit_strategy);
}
