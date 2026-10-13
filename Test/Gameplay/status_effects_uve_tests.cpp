// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/status_effects_uve.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

TEST(StatusEffectsUVETest, Apply_AddsAndRefreshesWithoutStacking) {
    GameplayAttributesComponentUVE attributes;
    StatusEffectsComponentUVE effects;
    EXPECT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F));
    EXPECT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "mana", 5.0F, 9.0F));
    ASSERT_EQ(effects.effects.size(), 1U);
    EXPECT_EQ(effects.effects.front().attributeId, "mana");
    EXPECT_FLOAT_EQ(effects.effects.front().magnitudePerSecond, 5.0F);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 9.0F);

    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "", "stamina", 1.0F, 1.0F));
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "x", "", 1.0F, 1.0F));
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "x", "stamina", 1.0F, 0.0F));
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "x", "stamina",
                                      std::numeric_limits<float>::quiet_NaN(), 1.0F));
    EXPECT_TRUE(RemoveStatusEffectUVE(attributes, effects, "poison"));
    EXPECT_FALSE(RemoveStatusEffectUVE(attributes, effects, "poison"));
}

TEST(StatusEffectsUVETest, Tick_DamagesOverTimeThenExpires) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F));

    const auto first = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first.front().effectId, "poison");
    EXPECT_FLOAT_EQ(first.front().amount, 10.0F);
    EXPECT_FLOAT_EQ(first.front().remaining, 90.0F);
    EXPECT_TRUE(first.front().applied);
    EXPECT_FALSE(first.front().expired);
    EXPECT_FALSE(first.front().depleted);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 90.0F);

    static_cast<void>(TickGameplayAttributesUVE(attributes, &effects, 1.0F));
    const auto last = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(last.size(), 1U);
    EXPECT_TRUE(last.front().expired);
    EXPECT_TRUE(effects.effects.empty());
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 70.0F);
}

TEST(StatusEffectsUVETest, Tick_NegativeMagnitudeHeals) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "mana", 100.0F, 50.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "regen-aura", "mana", -20.0F, 10.0F));
    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, -20.0F);
    EXPECT_FLOAT_EQ(results.front().remaining, 70.0F);
    EXPECT_TRUE(results.front().applied);
}

TEST(StatusEffectsUVETest, Tick_RegenDriftsWithoutEffects) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 50.0F, 2.0F));
    const auto results = TickGameplayAttributesUVE(attributes, nullptr, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results.front().effectId.empty());
    EXPECT_FLOAT_EQ(results.front().amount, -2.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 52.0F);
}

TEST(StatusEffectsUVETest, Tick_HealthTargetedEffectsAreComputedNotApplied) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "bleed", "health", 5.0F, 2.0F));
    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().attributeId, "health");
    EXPECT_FLOAT_EQ(results.front().amount, 5.0F);
    EXPECT_FALSE(results.front().applied);
    EXPECT_FALSE(results.front().expired);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 100.0F);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 1.0F);
}

TEST(StatusEffectsUVETest, Tick_MissingPoolIsSkippedButAged) {
    GameplayAttributesComponentUVE attributes;
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "mana", 5.0F, 1.0F));
    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FALSE(results.front().applied);
    EXPECT_TRUE(results.front().expired);
    EXPECT_TRUE(effects.effects.empty());
}

TEST(StatusEffectsUVETest, Tick_DepletingPoisonReportsDepleted) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 10.0F, 5.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 50.0F, 10.0F));
    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, 5.0F);
    EXPECT_TRUE(results.front().depleted);
}

TEST(StatusEffectsUVETest, Tick_InvalidStepTouchesNothing) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 50.0F, 5.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 5.0F, 5.0F));
    EXPECT_TRUE(TickGameplayAttributesUVE(attributes, &effects, 0.0F).empty());
    EXPECT_TRUE(TickGameplayAttributesUVE(attributes, &effects, -1.0F).empty());
    EXPECT_TRUE(TickGameplayAttributesUVE(attributes, &effects, std::numeric_limits<float>::quiet_NaN()).empty());
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 50.0F);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 5.0F);
}

TEST(StatusEffectsUVETest, Validity_RejectsBadIdsTimeAndOverflow) {
    GameplayAttributesComponentUVE attributes;
    StatusEffectsComponentUVE good;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, good, "poison", "stamina", 5.0F, 5.0F));
    EXPECT_TRUE(IsStatusEffectsComponentValidUVE(good));

    StatusEffectsComponentUVE duplicate = good;
    duplicate.effects.push_back(duplicate.effects.front());
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(duplicate));

    StatusEffectsComponentUVE negative = good;
    negative.effects.front().remainingSeconds = -1.0F;
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(negative));

    StatusEffectsComponentUVE overfull;
    for (std::size_t index = 0U; index <= kMaximumStatusEffectsUVE; ++index) {
        overfull.effects.push_back(
            StatusEffectUVE{"e" + std::to_string(index), "stamina", 1.0F, 1.0F});
    }
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(overfull));
}

TEST(StatusEffectsUVETest, Stacking_StacksAccumulateToCapAndMultiplyDrift) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 3U));
    ASSERT_EQ(effects.effects.front().stacks, 1U);
    static_cast<void>(TickGameplayAttributesUVE(attributes, &effects, 1.0F));
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 3U));
    ASSERT_EQ(effects.effects.size(), 1U);
    EXPECT_EQ(effects.effects.front().stacks, 2U);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 3.0F);
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 3U));
    EXPECT_EQ(effects.effects.front().stacks, 3U);

    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, 30.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 60.0F);

    // A refresh application collapses the instance back to a single stack ...
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F));
    EXPECT_EQ(effects.effects.front().stacks, 1U);
    // ... and a shrunken cap clamps the stacks it keeps.
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStackingUVE::Stack, 1U));
    EXPECT_EQ(effects.effects.front().stacks, 1U);
    EXPECT_EQ(effects.effects.front().maxStacks, 1U);
}

TEST(StatusEffectsUVETest, MaxDelta_InflatesPoolAndUnwindsOnRemoveAndExpiry) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 30.0F,
                                     StatusEffectStackingUVE::Refresh, 1U, 50.0F));
    EXPECT_TRUE(effects.effects.front().maxApplied);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 150.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 100.0F);

    ASSERT_TRUE(RemoveStatusEffectUVE(attributes, effects, "might"));
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 100.0F);
    EXPECT_TRUE(effects.effects.empty());

    // Expiry unwinds too, clamping an overfilled pool into the restored ceiling.
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 1.0F,
                                     StatusEffectStackingUVE::Refresh, 1U, 50.0F));
    GameplayAttributeUVE* pool = FindGameplayAttributeUVE(attributes, "stamina");
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(HealGameplayAttributeUVE(*pool, 50.0F).applied);
    ASSERT_FLOAT_EQ(attributes.attributes.front().current, 150.0F);
    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results.front().expired);
    EXPECT_FALSE(results.front().depleted);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 100.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 100.0F);
    EXPECT_TRUE(effects.effects.empty());
}

TEST(StatusEffectsUVETest, MaxDelta_StaysPendingUntilThePoolExists) {
    GameplayAttributesComponentUVE attributes;
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 30.0F,
                                     StatusEffectStackingUVE::Refresh, 1U, 25.0F));
    EXPECT_FALSE(effects.effects.front().maxApplied);

    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    static_cast<void>(TickGameplayAttributesUVE(attributes, &effects, 1.0F));
    EXPECT_TRUE(effects.effects.front().maxApplied);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 125.0F);
}

TEST(StatusEffectsUVETest, MaxDelta_RejectsHealthBrokenPoolsAndBadCaps) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponentUVE effects;
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "might", "health", 0.0F, 10.0F,
                                      StatusEffectStackingUVE::Refresh, 1U, 25.0F));
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      StatusEffectStackingUVE::Refresh, 0U, 25.0F));
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      StatusEffectStackingUVE::Refresh, 1U,
                                      std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      static_cast<StatusEffectStackingUVE>(7U)));
    EXPECT_TRUE(effects.effects.empty());

    attributes.attributes.front().maximum = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      StatusEffectStackingUVE::Refresh, 1U, 25.0F));
    EXPECT_TRUE(effects.effects.empty());
}

TEST(StatusEffectsUVETest, Tick_StacksMultiplyHealthDrift) {
    GameplayAttributesComponentUVE attributes;
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "bleed", "health", 5.0F, 2.0F,
                                     StatusEffectStackingUVE::Stack, 4U));
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "bleed", "health", 5.0F, 2.0F,
                                     StatusEffectStackingUVE::Stack, 4U));
    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, 10.0F);
    EXPECT_FALSE(results.front().applied);
}

TEST(StatusEffectsUVETest, Tick_ExpiryReversalFlagsDepletedWhenTheCeilingHitsZero) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 60.0F, 60.0F));
    StatusEffectsComponentUVE effects;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, effects, "might", "stamina", 0.0F, 1.0F,
                                     StatusEffectStackingUVE::Refresh, 1U, 50.0F));
    ASSERT_FLOAT_EQ(attributes.attributes.front().maximum, 110.0F);
    // External tampering drops the ceiling below the granted shift; unwinding floors at zero.
    attributes.attributes.front().maximum = 10.0F;
    const auto results = TickGameplayAttributesUVE(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results.front().expired);
    EXPECT_TRUE(results.front().depleted);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 0.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 0.0F);
}

TEST(StatusEffectsUVETest, Validity_RejectsBadStackCountsPoliciesAndMaxDelta) {
    GameplayAttributesComponentUVE attributes;
    StatusEffectsComponentUVE good;
    ASSERT_TRUE(ApplyStatusEffectUVE(attributes, good, "might", "stamina", 0.0F, 5.0F,
                                     StatusEffectStackingUVE::Stack, 3U, 10.0F));
    EXPECT_TRUE(IsStatusEffectsComponentValidUVE(good));

    StatusEffectsComponentUVE unstacked = good;
    unstacked.effects.front().stacks = 0U;
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(unstacked));

    StatusEffectsComponentUVE overcapped = good;
    overcapped.effects.front().stacks = 4U;
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(overcapped));

    StatusEffectsComponentUVE nocap = good;
    nocap.effects.front().maxStacks = 0U;
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(nocap));

    StatusEffectsComponentUVE strange = good;
    strange.effects.front().stacking = static_cast<StatusEffectStackingUVE>(7U);
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(strange));

    StatusEffectsComponentUVE wild = good;
    wild.effects.front().maxDelta = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsStatusEffectsComponentValidUVE(wild));
}

} // namespace UVE::Scene::Tests
