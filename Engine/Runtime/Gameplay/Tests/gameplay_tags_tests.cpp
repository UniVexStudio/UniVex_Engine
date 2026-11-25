// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gameplay_tags.h"

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

TEST(GameplayTagsTest, AddHasRemove_RoundTrips) {
    GameplayTagComponent tags;
    EXPECT_FALSE(HasGameplayTag(tags, "undead"));
    EXPECT_TRUE(AddGameplayTag(tags, "undead"));
    EXPECT_TRUE(HasGameplayTag(tags, "undead"));
    EXPECT_FALSE(AddGameplayTag(tags, "undead"));
    EXPECT_TRUE(RemoveGameplayTag(tags, "undead"));
    EXPECT_FALSE(HasGameplayTag(tags, "undead"));
    EXPECT_FALSE(RemoveGameplayTag(tags, "undead"));
}

TEST(GameplayTagsTest, Add_RejectsBadTagsAndOverflow) {
    GameplayTagComponent tags;
    EXPECT_FALSE(AddGameplayTag(tags, ""));
    EXPECT_FALSE(AddGameplayTag(tags, std::string(49U, 'x')));
    for (std::size_t index = 0U; index < kMaximumGameplayTags; ++index) {
        EXPECT_TRUE(AddGameplayTag(tags, "tag" + std::to_string(index)));
    }
    EXPECT_FALSE(AddGameplayTag(tags, "overflow"));
}

TEST(GameplayTagsTest, HasAllAndHasAny_MatchSetSemantics) {
    GameplayTagComponent tags;
    ASSERT_TRUE(AddGameplayTag(tags, "undead"));
    ASSERT_TRUE(AddGameplayTag(tags, "boss"));
    EXPECT_TRUE(HasAllGameplayTags(tags, {"undead", "boss"}));
    EXPECT_FALSE(HasAllGameplayTags(tags, {"undead", "flammable"}));
    EXPECT_TRUE(HasAllGameplayTags(tags, {}));
    EXPECT_TRUE(HasAnyGameplayTag(tags, {"flammable", "boss"}));
    EXPECT_FALSE(HasAnyGameplayTag(tags, {"flammable", "flying"}));
    EXPECT_FALSE(HasAnyGameplayTag(tags, {}));
}

TEST(GameplayTagsTest, Validity_RejectsDuplicatesBadTagsAndOverflow) {
    GameplayTagComponent good;
    ASSERT_TRUE(AddGameplayTag(good, "undead"));
    EXPECT_TRUE(IsGameplayTagComponentValid(good));

    GameplayTagComponent duplicate = good;
    duplicate.tags.push_back("undead");
    EXPECT_FALSE(IsGameplayTagComponentValid(duplicate));

    GameplayTagComponent empty;
    empty.tags.emplace_back("");
    EXPECT_FALSE(IsGameplayTagComponentValid(empty));

    GameplayTagComponent overfull;
    for (std::size_t index = 0U; index <= kMaximumGameplayTags; ++index) {
        overfull.tags.push_back("t" + std::to_string(index));
    }
    EXPECT_FALSE(IsGameplayTagComponentValid(overfull));
}

} // namespace UVE::Scene::Tests
