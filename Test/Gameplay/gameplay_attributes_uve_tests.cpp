// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/gameplay_attributes_uve.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

TEST(GameplayAttributesUVETest, Add_StoresWithClampedInitial) {
    GameplayAttributesComponentUVE attributes;
    EXPECT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 120.0F));
    EXPECT_TRUE(AddGameplayAttributeUVE(attributes, "mana", 50.0F, -5.0F, 2.0F));
    ASSERT_EQ(attributes.attributes.size(), 2U);
    EXPECT_FLOAT_EQ(attributes.attributes[0].current, 100.0F);
    EXPECT_FLOAT_EQ(attributes.attributes[1].current, 0.0F);
    EXPECT_FLOAT_EQ(attributes.attributes[1].regenPerSecond, 2.0F);
    EXPECT_TRUE(IsGameplayAttributesComponentValidUVE(attributes));
}

TEST(GameplayAttributesUVETest, Add_RejectsBadIdsAndOverflow) {
    GameplayAttributesComponentUVE attributes;
    EXPECT_FALSE(AddGameplayAttributeUVE(attributes, "", 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttributeUVE(attributes, std::string(49U, 'x'), 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttributeUVE(attributes, "health", 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttributeUVE(attributes, "stamina", -1.0F, 0.0F));
    EXPECT_FALSE(AddGameplayAttributeUVE(attributes, "stamina",
                                         std::numeric_limits<float>::quiet_NaN(), 0.0F));
    EXPECT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 10.0F, 10.0F));
    EXPECT_FALSE(AddGameplayAttributeUVE(attributes, "stamina", 10.0F, 10.0F));
    for (std::size_t index = 1U; index < kMaximumGameplayAttributesUVE; ++index) {
        EXPECT_TRUE(AddGameplayAttributeUVE(attributes, "attr" + std::to_string(index), 1.0F, 1.0F));
    }
    EXPECT_FALSE(AddGameplayAttributeUVE(attributes, "overflow", 1.0F, 1.0F));
}

TEST(GameplayAttributesUVETest, Remove_ErasesOnlyWhatExists) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 10.0F, 10.0F));
    EXPECT_FALSE(RemoveGameplayAttributeUVE(attributes, "mana"));
    EXPECT_TRUE(RemoveGameplayAttributeUVE(attributes, "stamina"));
    EXPECT_TRUE(attributes.attributes.empty());
}

TEST(GameplayAttributesUVETest, Damage_ReducesAndDepletes) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 100.0F));
    GameplayAttributeUVE& stamina = attributes.attributes.front();

    const GameplayAttributeDamageResultUVE first = DamageGameplayAttributeUVE(stamina, 30.0F);
    EXPECT_TRUE(first.applied);
    EXPECT_FLOAT_EQ(first.amount, 30.0F);
    EXPECT_FLOAT_EQ(first.remaining, 70.0F);
    EXPECT_FALSE(first.depleted);

    const GameplayAttributeDamageResultUVE overkill = DamageGameplayAttributeUVE(stamina, 1000.0F);
    EXPECT_TRUE(overkill.applied);
    EXPECT_FLOAT_EQ(overkill.amount, 70.0F);
    EXPECT_FLOAT_EQ(overkill.remaining, 0.0F);
    EXPECT_TRUE(overkill.depleted);

    const GameplayAttributeDamageResultUVE empty = DamageGameplayAttributeUVE(stamina, 10.0F);
    EXPECT_TRUE(empty.applied);
    EXPECT_FLOAT_EQ(empty.amount, 0.0F);
    EXPECT_TRUE(empty.depleted);

    EXPECT_FALSE(DamageGameplayAttributeUVE(stamina, 0.0F).applied);
    EXPECT_FALSE(DamageGameplayAttributeUVE(stamina, -5.0F).applied);
    EXPECT_FALSE(DamageGameplayAttributeUVE(stamina, std::numeric_limits<float>::quiet_NaN()).applied);
    EXPECT_FLOAT_EQ(stamina.current, 0.0F);
}

TEST(GameplayAttributesUVETest, Heal_ClampsAtMaximum) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 70.0F));
    GameplayAttributeUVE& stamina = attributes.attributes.front();

    const GameplayAttributeDamageResultUVE healed = HealGameplayAttributeUVE(stamina, 50.0F);
    EXPECT_TRUE(healed.applied);
    EXPECT_FLOAT_EQ(healed.amount, 30.0F);
    EXPECT_FLOAT_EQ(healed.remaining, 100.0F);
    EXPECT_FALSE(healed.depleted);

    EXPECT_FALSE(HealGameplayAttributeUVE(stamina, 0.0F).applied);
    EXPECT_FALSE(HealGameplayAttributeUVE(stamina, -5.0F).applied);
}

TEST(GameplayAttributesUVETest, SetMaximum_ClampsCurrentDown) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 100.0F, 80.0F));
    GameplayAttributeUVE& stamina = attributes.attributes.front();
    EXPECT_TRUE(SetGameplayAttributeMaximumUVE(stamina, 50.0F));
    EXPECT_FLOAT_EQ(stamina.maximum, 50.0F);
    EXPECT_FLOAT_EQ(stamina.current, 50.0F);
    EXPECT_FALSE(SetGameplayAttributeMaximumUVE(stamina, -1.0F));
    EXPECT_FLOAT_EQ(stamina.maximum, 50.0F);
}

TEST(GameplayAttributesUVETest, Find_ReturnsNullWhenMissing) {
    GameplayAttributesComponentUVE attributes;
    ASSERT_TRUE(AddGameplayAttributeUVE(attributes, "stamina", 10.0F, 10.0F));
    EXPECT_NE(FindGameplayAttributeUVE(attributes, "stamina"), nullptr);
    EXPECT_EQ(FindGameplayAttributeUVE(attributes, "mana"), nullptr);
    const GameplayAttributesComponentUVE& frozen = attributes;
    EXPECT_NE(FindGameplayAttributeUVE(frozen, "stamina"), nullptr);
    EXPECT_EQ(FindGameplayAttributeUVE(frozen, "mana"), nullptr);
}

TEST(GameplayAttributesUVETest, Validity_RejectsDuplicatesReservedAndOverflow) {
    GameplayAttributesComponentUVE good;
    ASSERT_TRUE(AddGameplayAttributeUVE(good, "stamina", 10.0F, 10.0F));
    EXPECT_TRUE(IsGameplayAttributesComponentValidUVE(good));

    GameplayAttributesComponentUVE duplicate = good;
    duplicate.attributes.push_back(duplicate.attributes.front());
    EXPECT_FALSE(IsGameplayAttributesComponentValidUVE(duplicate));

    GameplayAttributesComponentUVE reserved;
    reserved.attributes.push_back(GameplayAttributeUVE{"health", 10.0F, 10.0F, 0.0F});
    EXPECT_FALSE(IsGameplayAttributesComponentValidUVE(reserved));

    GameplayAttributesComponentUVE overfull;
    for (std::size_t index = 0U; index <= kMaximumGameplayAttributesUVE; ++index) {
        overfull.attributes.push_back(GameplayAttributeUVE{"a" + std::to_string(index), 1.0F, 1.0F, 0.0F});
    }
    EXPECT_FALSE(IsGameplayAttributesComponentValidUVE(overfull));

    GameplayAttributesComponentUVE overMax = good;
    overMax.attributes.front().current = 11.0F;
    EXPECT_FALSE(IsGameplayAttributesComponentValidUVE(overMax));
}

} // namespace UVE::Scene::Tests
