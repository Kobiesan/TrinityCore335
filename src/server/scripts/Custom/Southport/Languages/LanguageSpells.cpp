/*
 * Languages-as-professions: the "study book" spell effect.
 * Ported verbatim in behaviour from the Twinkcraft core
 * (src/server/scripts/Spells/spell_generic.cpp, spell_increase_skill_point).
 *
 * Every lore book's study spell uses SPELL_EFFECT_SCRIPT_EFFECT (77) and is bound
 * by `spell_script_names.ScriptName = 'spell_increase_skill_point'`:
 *   EffectMiscValue  = language skill line (98 Common, 789 Kalimag, ...)
 *   EffectMiscValueB = this text's skill cap (10, 25, 50 .. 300)
 *   EffectBasePoints = skill points gained per use
 */
#include "SpellScript.h"
#include "Player.h"

class spell_increase_skill_point : public SpellScript
{
    PrepareSpellScript(spell_increase_skill_point);

    void HandleScript(SpellEffIndex /*effIndex*/)
    {
        if (Player* player = GetHitPlayer())
        {
            uint32 skillId = uint32(GetEffectInfo().MiscValue);
            int32 amount = GetEffectValue();
            int32 skillCap = GetEffectInfo().MiscValueB;

            uint16 currentValue = player->GetSkillValue(skillId);
            uint16 maxValue = player->GetMaxSkillValue(skillId);

            uint16 effectiveMax = skillCap > 0 ? std::min<uint16>(uint16(skillCap), maxValue) : maxValue;

            if (currentValue && currentValue < effectiveMax)
            {
                uint16 newValue = uint16(std::min<int32>(int32(currentValue) + amount, int32(effectiveMax)));
                player->SetSkill(skillId, player->GetSkillStep(skillId), newValue, maxValue);
            }
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_increase_skill_point::HandleScript, EFFECT_0, SPELL_EFFECT_SCRIPT_EFFECT);
    }
};

void AddSC_southport_language_spells()
{
    RegisterSpellScript(spell_increase_skill_point);
}
