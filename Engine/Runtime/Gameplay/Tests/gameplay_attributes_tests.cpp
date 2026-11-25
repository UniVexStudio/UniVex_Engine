// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gameplay_attributes.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

TEST(GameplayAttributesTest, Add_StoresWithClampedInitial) {
    GameplayAttributesComponent attributes;
    EXPECT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 120.0F));
    EXPECT_TRUE(AddGameplayAttribute(attributes, "mana", 50.0F, -5.0F, 2.0F));
    ASSERT_EQ(attributes.attributes.size(), 2U);
    EXPECT_FLOAT_EQ(attributes.attributes[0].current, 100.0F);
    EXPECT_FLOAT_EQ(attributes.attributes[1].current, 0.0F);
    EXPECT_FLOAT_EQ(attributes.attributes[1].regenPerSecond, 2.0F);
    EXPECT_TRUE(IsGameplayAttributesComponentValid(attributes));
}

TEST(GameplayAttributesTest, Add_RejectsBadIdsAndOverflow) {
    GameplayAttributesComponent attributes;
    EXPECT_FALSE(AddGameplayAttribute(attributes, "", 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttribute(attributes, std::string(49U, 'x'), 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttribute(attributes, "health", 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttribute(attributes, "stamina", -1.0F, 0.0F));
    EXPECT_FALSE(AddGameplayAttribute(attributes, "stamina",
                                         std::numeric_limits<float>::quiet_NaN(), 0.0F));
    EXPECT_TRUE(AddGameplayAttribute(attributes, "stamina", 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttribute(attributes, "stamina", 10.0F, 10.0F));
    for (std::size_t index = 1U; index < kMaximumGameplayAttributes; ++index) {
        EXPECT_TRUE(AddGameplayAttribute(attributes, "attr" + std::to_string(index), 1.0F, 1.0F));
    }
    EXPECT_FALSE(AddGameplayAttribute(attributes, "overflow", 1.0F, 1.0F));
}

TEST(GameplayAttributesTest, Remove_ErasesOnlyWhatExists) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 10.0F, 10.0F));
    EXPECT_FALSE(RemoveGameplayAttribute(attributes, "mana"));
    EXPECT_TRUE(RemoveGameplayAttribute(attributes, "stamina"));
    EXPECT_TRUE(attributes.attributes.empty());
}

TEST(GameplayAttributesTest, Damage_ReducesAndDepletes) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 100.0F));
    GameplayAttribute& stamina = attributes.attributes.front();

    const GameplayAttributeDamageResult first = DamageGameplayAttribute(stamina, 30.0F);
    EXPECT_TRUE(first.applied);
    EXPECT_FLOAT_EQ(first.amount, 30.0F);
    EXPECT_FLOAT_EQ(first.remaining, 70.0F);
    EXPECT_FALSE(first.depleted);

    const GameplayAttributeDamageResult overkill = DamageGameplayAttribute(stamina, 1000.0F);
    EXPECT_TRUE(overkill.applied);
    EXPECT_FLOAT_EQ(overkill.amount, 70.0F);
    EXPECT_FLOAT_EQ(overkill.remaining, 0.0F);
    EXPECT_TRUE(overkill.depleted);

    const GameplayAttributeDamageResult empty = DamageGameplayAttribute(stamina, 10.0F);
    EXPECT_TRUE(empty.applied);
    EXPECT_FLOAT_EQ(empty.amount, 0.0F);
    EXPECT_TRUE(empty.depleted);

    EXPECT_FALSE(DamageGameplayAttribute(stamina, 0.0F).applied);
    EXPECT_FALSE(DamageGameplayAttribute(stamina, -5.0F).applied);
    EXPECT_FALSE(DamageGameplayAttribute(stamina, std::numeric_limits<float>::quiet_NaN()).applied);
    EXPECT_FLOAT_EQ(stamina.current, 0.0F);
}

TEST(GameplayAttributesTest, Heal_ClampsAtMaximum) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 70.0F));
    GameplayAttribute& stamina = attributes.attributes.front();

    const GameplayAttributeDamageResult healed = HealGameplayAttribute(stamina, 50.0F);
    EXPECT_TRUE(healed.applied);
    EXPECT_FLOAT_EQ(healed.amount, 30.0F);
    EXPECT_FLOAT_EQ(healed.remaining, 100.0F);
    EXPECT_FALSE(healed.depleted);

    EXPECT_FALSE(HealGameplayAttribute(stamina, 0.0F).applied);
    EXPECT_FALSE(HealGameplayAttribute(stamina, -5.0F).applied);
}

TEST(GameplayAttributesTest, SetMaximum_ClampsCurrentDown) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 100.0F, 80.0F));
    GameplayAttribute& stamina = attributes.attributes.front();
    EXPECT_TRUE(SetGameplayAttributeMaximum(stamina, 50.0F));
    EXPECT_FLOAT_EQ(stamina.maximum, 50.0F);
    EXPECT_FLOAT_EQ(stamina.current, 50.0F);
    EXPECT_FALSE(SetGameplayAttributeMaximum(stamina, -1.0F));
    EXPECT_FLOAT_EQ(stamina.maximum, 50.0F);
}

TEST(GameplayAttributesTest, Find_ReturnsNullWhenMissing) {
    GameplayAttributesComponent attributes;
    ASSERT_TRUE(AddGameplayAttribute(attributes, "stamina", 10.0F, 10.0F));
    EXPECT_NE(FindGameplayAttribute(attributes, "stamina"), nullptr);
    EXPECT_EQ(FindGameplayAttribute(attributes, "mana"), nullptr);
    const GameplayAttributesComponent& frozen = attributes;
    EXPECT_NE(FindGameplayAttribute(frozen, "stamina"), nullptr);
    EXPECT_EQ(FindGameplayAttribute(frozen, "mana"), nullptr);
}

TEST(GameplayAttributesTest, Validity_RejectsDuplicatesReservedAndOverflow) {
    GameplayAttributesComponent good;
    ASSERT_TRUE(AddGameplayAttribute(good, "stamina", 10.0F, 10.0F));
    EXPECT_TRUE(IsGameplayAttributesComponentValid(good));

    GameplayAttributesComponent duplicate = good;
    duplicate.attributes.push_back(duplicate.attributes.front());
    EXPECT_FALSE(IsGameplayAttributesComponentValid(duplicate));

    GameplayAttributesComponent reserved;
    reserved.attributes.push_back(GameplayAttribute{"health", 10.0F, 10.0F, 0.0F});
    EXPECT_FALSE(IsGameplayAttributesComponentValid(reserved));

    GameplayAttributesComponent overfull;
    for (std::size_t index = 0U; index <= kMaximumGameplayAttributes; ++index) {
        overfull.attributes.push_back(GameplayAttribute{"a" + std::to_string(index), 1.0F, 1.0F, 0.0F});
    }
    EXPECT_FALSE(IsGameplayAttributesComponentValid(overfull));

    GameplayAttributesComponent overMax = good;
    overMax.attributes.front().current = 11.0F;
    EXPECT_FALSE(IsGameplayAttributesComponentValid(overMax));
}

} // namespace UVE::Scene::Tests
