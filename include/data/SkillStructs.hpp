#pragma once
#include <string>
#include <vector>

/**
 * @brief Definition of a single effect applied by an active skill on cast.
 *
 * For buff/debuff skills these are loaded from the skill_active_effects table.
 * For passive skills they come from passive_skill_modifiers.
 * Damage/heal skills typically have an empty effects list (handled by the
 * combat calculator via coeff/flatAdd).
 */
struct SkillEffectDefinitionStruct
{
    std::string effectSlug;       ///< unique slug, e.g. "battle_cry_phys_atk"
    std::string effectTypeSlug;   ///< "buff" | "debuff" | "dot" | "hot" | "passive"
    std::string attributeSlug;    ///< target attribute, e.g. "physical_attack"
    float value = 0.0f;           ///< stat modifier or per-tick amount
    int durationSeconds = 0;      ///< 0 = permanent; >0 = timed
    int tickMs = 0;               ///< 0 = non-tick; >0 = DoT/HoT interval in ms
};

// Структура для скила персонажа/моба
struct SkillStruct
{
    std::string skillName = "";       // Название скила
    std::string skillSlug = "";       // Slug скила
    std::string scaleStat = "";       // От какого атрибута зависит
    std::string school = "";          // Школа магии (physical, fire, ice, etc.)
    std::string skillEffectType = ""; // Тип эффекта (damage, heal, buff, etc.)
    int skillLevel = 1;               // Уровень скила

    // Характеристики урона
    float coeff = 0.0f;   // Коэффициент масштабирования
    float flatAdd = 0.0f; // Плоская добавка

    // Характеристики скила
    int cooldownMs = 0;             // Время восстановления в мс
    int gcdMs = 0;                  // Глобальный КД в мс
    int castMs = 0;                 // Время каста в мс
    int costMp = 0;                 // Стоимость маны
    float maxRange = 0.0f;          // Максимальная дальность
    float areaRadius = 0.0f;        // Радиус AoE (0 = single target)
    int swingMs = 300;              // Длительность анимации свинга (после каста), мс
    std::string animationName = ""; // Название анимационного клипа кастера (Unity Animator state)

    // Passive skill flag: if true the skill is always-on and never cast actively.
    // Its modifiers arrive as ActiveEffectStruct entries (sourceType="skill_passive", expiresAt=0).
    bool isPassive = false;

    /**
     * @brief Effects applied on cast (buff/debuff/dot/hot for active skills;
     *        stat modifiers for passive skills, loaded from passive_skill_modifiers).
     *
     * For active skills: populated from skill_active_effects at character join/skill-learn.
     * For passive skills: populated from passive_skill_modifiers at skill-learn.
     * For damage/heal skills: typically empty — CombatCalculator handles value via coeff/flatAdd.
     */
    std::vector<SkillEffectDefinitionStruct> effects;
};

// Структура для атрибута entity (персонажа или моба)
struct EntityAttributeStruct
{
    int id = 0;
    std::string name = "";
    std::string slug = "";
    int value = 0;
};

// Структура для расчета урона
struct DamageCalculationStruct
{
    int baseDamage = 0;
    int scaledDamage = 0;
    int totalDamage = 0;
    bool isCritical = false;
    bool isBlocked = false;
    bool isMissed = false;
    std::string damageType = ""; // physical, magical, true
};

// Структура для передачи скилов персонажа с ID
struct CharacterSkillStruct
{
    int characterId = 0;
    SkillStruct skill;
};

// Структура для результата использования скила
struct SkillUsageResult
{
    bool success = false;
    std::string errorMessage = "";
    DamageCalculationStruct damageResult;
    int healAmount = 0;
    std::vector<std::string> appliedEffects;
};
