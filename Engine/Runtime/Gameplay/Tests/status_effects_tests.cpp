// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "status_effects.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

TEST(StatusEffectsTest, Apply_AddsAndRefreshesWithoutStacking) {
    GameplayAttributesComponent attributes;
    StatusEffectsComponent effects;
    EXPECT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F));
    EXPECT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "mana", 5.0F, 9.0F));
    ASSERT_EQ(effects.effects.size(), 1U);
    EXPECT_EQ(effects.effects.front().attributeId, "mana");
    EXPECT_FLOAT_EQ(effects.effects.front().magnitudePerSecond, 5.0F);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 9.0F);

    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "", "stamina", 1.0F, 1.0F));
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "x", "", 1.0F, 1.0F));
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "x", "stamina", 1.0F, 0.0F));
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "x", "stamina",
                                      std::numeric_limits<float>::quiet_NaN(), 1.0F));
    EXPECT_TRUE(RemoveStatusEffect(attributes, effects, "poison"));
    EXPECT_FALSE(RemoveStatusEffect(attributes, effects, "poison"));
}

TEST(StatusEffectsTest, Tick_DamagesOverTimeThenExpires) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F));

    const auto first = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first.front().effectId, "poison");
    EXPECT_FLOAT_EQ(first.front().amount, 10.0F);
    EXPECT_FLOAT_EQ(first.front().remaining, 90.0F);
    EXPECT_TRUE(first.front().applied);
    EXPECT_FALSE(first.front().expired);
    EXPECT_FALSE(first.front().depleted);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 90.0F);

    static_cast<void>(TickGameplayAttributes(attributes, &effects, 1.0F));
    const auto last = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(last.size(), 1U);
    EXPECT_TRUE(last.front().expired);
    EXPECT_TRUE(effects.effects.empty());
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 70.0F);
}

TEST(StatusEffectsTest, Tick_NegativeMagnitudeHeals) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "mana", 100.0F, 50.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "regen-aura", "mana", -20.0F, 10.0F));
    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, -20.0F);
    EXPECT_FLOAT_EQ(results.front().remaining, 70.0F);
    EXPECT_TRUE(results.front().applied);
}

TEST(StatusEffectsTest, Tick_RegenDriftsWithoutEffects) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 50.0F, 2.0F));
    const auto results = TickGameplayAttributes(attributes, nullptr, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results.front().effectId.empty());
    EXPECT_FLOAT_EQ(results.front().amount, -2.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 52.0F);
}

TEST(StatusEffectsTest, Tick_HealthTargetedEffectsAreComputedNotApplied) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "bleed", "health", 5.0F, 2.0F));
    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results.front().attributeId, "health");
    EXPECT_FLOAT_EQ(results.front().amount, 5.0F);
    EXPECT_FALSE(results.front().applied);
    EXPECT_FALSE(results.front().expired);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 100.0F);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 1.0F);
}

TEST(StatusEffectsTest, Tick_MissingPoolIsSkippedButAged) {
    GameplayAttributesComponent attributes;
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "mana", 5.0F, 1.0F));
    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FALSE(results.front().applied);
    EXPECT_TRUE(results.front().expired);
    EXPECT_TRUE(effects.effects.empty());
}

TEST(StatusEffectsTest, Tick_DepletingPoisonReportsDepleted) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 10.0F, 5.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 50.0F, 10.0F));
    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, 5.0F);
    EXPECT_TRUE(results.front().depleted);
}

TEST(StatusEffectsTest, Tick_InvalidStepTouchesNothing) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 50.0F, 5.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 5.0F, 5.0F));
    EXPECT_TRUE(TickGameplayAttributes(attributes, &effects, 0.0F).empty());
    EXPECT_TRUE(TickGameplayAttributes(attributes, &effects, -1.0F).empty());
    EXPECT_TRUE(TickGameplayAttributes(attributes, &effects, std::numeric_limits<float>::quiet_NaN()).empty());
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 50.0F);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 5.0F);
}

TEST(StatusEffectsTest, Validity_RejectsBadIdsTimeAndOverflow) {
    GameplayAttributesComponent attributes;
    StatusEffectsComponent good;
    ASSERT_TRUE(ApplyStatusEffect(attributes, good, "poison", "stamina", 5.0F, 5.0F));
    EXPECT_TRUE(IsStatusEffectsComponentValid(good));

    StatusEffectsComponent duplicate = good;
    duplicate.effects.push_back(duplicate.effects.front());
    EXPECT_FALSE(IsStatusEffectsComponentValid(duplicate));

    StatusEffectsComponent negative = good;
    negative.effects.front().remainingSeconds = -1.0F;
    EXPECT_FALSE(IsStatusEffectsComponentValid(negative));

    StatusEffectsComponent overfull;
    for (std::size_t index = 0U; index <= kMaximumStatusEffects; ++index) {
        overfull.effects.push_back(
            StatusEffect{"e" + std::to_string(index), "stamina", 1.0F, 1.0F});
    }
    EXPECT_FALSE(IsStatusEffectsComponentValid(overfull));
}

TEST(StatusEffectsTest, Stacking_StacksAccumulateToCapAndMultiplyDrift) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 3U));
    ASSERT_EQ(effects.effects.front().stacks, 1U);
    static_cast<void>(TickGameplayAttributes(attributes, &effects, 1.0F));
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 3U));
    ASSERT_EQ(effects.effects.size(), 1U);
    EXPECT_EQ(effects.effects.front().stacks, 2U);
    EXPECT_FLOAT_EQ(effects.effects.front().remainingSeconds, 3.0F);
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 3U));
    EXPECT_EQ(effects.effects.front().stacks, 3U);

    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, 30.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 60.0F);

    // A refresh application collapses the instance back to a single stack ...
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F));
    EXPECT_EQ(effects.effects.front().stacks, 1U);
    // ... and a shrunken cap clamps the stacks it keeps.
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 3U));
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "poison", "stamina", 10.0F, 3.0F,
                                     StatusEffectStacking::Stack, 1U));
    EXPECT_EQ(effects.effects.front().stacks, 1U);
    EXPECT_EQ(effects.effects.front().maxStacks, 1U);
}

TEST(StatusEffectsTest, MaxDelta_InflatesPoolAndUnwindsOnRemoveAndExpiry) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 30.0F,
                                     StatusEffectStacking::Refresh, 1U, 50.0F));
    EXPECT_TRUE(effects.effects.front().maxApplied);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 150.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 100.0F);

    ASSERT_TRUE(RemoveStatusEffect(attributes, effects, "might"));
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 100.0F);
    EXPECT_TRUE(effects.effects.empty());

    // Expiry unwinds too, clamping an overfilled pool into the restored ceiling.
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 1.0F,
                                     StatusEffectStacking::Refresh, 1U, 50.0F));
    GameplayAttribute* pool = FindGameplayAttribute(attributes, "stamina");
    ASSERT_NE(pool, nullptr);
    ASSERT_TRUE(HealGameplayAttribute(*pool, 50.0F).applied);
    ASSERT_FLOAT_EQ(attributes.attributes.front().current, 150.0F);
    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results.front().expired);
    EXPECT_FALSE(results.front().depleted);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 100.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 100.0F);
    EXPECT_TRUE(effects.effects.empty());
}

TEST(StatusEffectsTest, MaxDelta_StaysPendingUntilThePoolExists) {
    GameplayAttributesComponent attributes;
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 30.0F,
                                     StatusEffectStacking::Refresh, 1U, 25.0F));
    EXPECT_FALSE(effects.effects.front().maxApplied);

    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    static_cast<void>(TickGameplayAttributes(attributes, &effects, 1.0F));
    EXPECT_TRUE(effects.effects.front().maxApplied);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 125.0F);
}

TEST(StatusEffectsTest, MaxDelta_RejectsHealthBrokenPoolsAndBadCaps) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    StatusEffectsComponent effects;
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "might", "health", 0.0F, 10.0F,
                                      StatusEffectStacking::Refresh, 1U, 25.0F));
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      StatusEffectStacking::Refresh, 0U, 25.0F));
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      StatusEffectStacking::Refresh, 1U,
                                      std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      static_cast<StatusEffectStacking>(7U)));
    EXPECT_TRUE(effects.effects.empty());

    attributes.attributes.front().maximum = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 10.0F,
                                      StatusEffectStacking::Refresh, 1U, 25.0F));
    EXPECT_TRUE(effects.effects.empty());
}

TEST(StatusEffectsTest, Tick_StacksMultiplyHealthDrift) {
    GameplayAttributesComponent attributes;
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "bleed", "health", 5.0F, 2.0F,
                                     StatusEffectStacking::Stack, 4U));
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "bleed", "health", 5.0F, 2.0F,
                                     StatusEffectStacking::Stack, 4U));
    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_FLOAT_EQ(results.front().amount, 10.0F);
    EXPECT_FALSE(results.front().applied);
}

TEST(StatusEffectsTest, Tick_ExpiryReversalFlagsDepletedWhenTheCeilingHitsZero) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 60.0F, 60.0F));
    StatusEffectsComponent effects;
    ASSERT_TRUE(ApplyStatusEffect(attributes, effects, "might", "stamina", 0.0F, 1.0F,
                                     StatusEffectStacking::Refresh, 1U, 50.0F));
    ASSERT_FLOAT_EQ(attributes.attributes.front().maximum, 110.0F);
    // External tampering drops the ceiling below the granted shift; unwinding floors at zero.
    attributes.attributes.front().maximum = 10.0F;
    const auto results = TickGameplayAttributes(attributes, &effects, 1.0F);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_TRUE(results.front().expired);
    EXPECT_TRUE(results.front().depleted);
    EXPECT_FLOAT_EQ(attributes.attributes.front().maximum, 0.0F);
    EXPECT_FLOAT_EQ(attributes.attributes.front().current, 0.0F);
}

TEST(StatusEffectsTest, Validity_RejectsBadStackCountsPoliciesAndMaxDelta) {
    GameplayAttributesComponent attributes;
    StatusEffectsComponent good;
    ASSERT_TRUE(ApplyStatusEffect(attributes, good, "might", "stamina", 0.0F, 5.0F,
                                     StatusEffectStacking::Stack, 3U, 10.0F));
    EXPECT_TRUE(IsStatusEffectsComponentValid(good));

    StatusEffectsComponent unstacked = good;
    unstacked.effects.front().stacks = 0U;
    EXPECT_FALSE(IsStatusEffectsComponentValid(unstacked));

    StatusEffectsComponent overcapped = good;
    overcapped.effects.front().stacks = 4U;
    EXPECT_FALSE(IsStatusEffectsComponentValid(overcapped));

    StatusEffectsComponent nocap = good;
    nocap.effects.front().maxStacks = 0U;
    EXPECT_FALSE(IsStatusEffectsComponentValid(nocap));

    StatusEffectsComponent strange = good;
    strange.effects.front().stacking = static_cast<StatusEffectStacking>(7U);
    EXPECT_FALSE(IsStatusEffectsComponentValid(strange));

    StatusEffectsComponent wild = good;
    wild.effects.front().maxDelta = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsStatusEffectsComponentValid(wild));
}

} // namespace UVE::Scene::Tests
