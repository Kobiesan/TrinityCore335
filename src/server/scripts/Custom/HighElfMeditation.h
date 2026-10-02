#ifndef TBV_HIGH_ELF_MEDITATION_H
#define TBV_HIGH_ELF_MEDITATION_H

#include "SpellScript.h"
#include "Unit.h"

// The DBC permits casting while silenced. Remove the actual aura types rather
// than relying on every silence spell having the same mechanic/dispel flags.
class spell_tbv_queldorei_meditation : public SpellScript
{
    PrepareSpellScript(spell_tbv_queldorei_meditation);

    void ClearSilence()
    {
        GetCaster()->RemoveAurasByType(SPELL_AURA_MOD_SILENCE);
        GetCaster()->RemoveAurasByType(SPELL_AURA_MOD_PACIFY_SILENCE);
    }

    void Register() override
    {
        OnCast += SpellCastFn(spell_tbv_queldorei_meditation::ClearSilence);
    }
};

#endif
