// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/gameplay_tags_uve.h"

#include <gtest/gtest.h>

namespace UVE::Scene::Tests {

TEST(GameplayTagsUVETest, AddHasRemove_RoundTrips) {
    GameplayTagComponentUVE tags;
    EXPECT_FALSE(HasGameplayTagUVE(tags, "undead"));
    EXPECT_TRUE(AddGameplayTagUVE(tags, "undead"));
    EXPECT_TRUE(HasGameplayTagUVE(tags, "undead"));
    EXPECT_FALSE(AddGameplayTagUVE(tags, "undead"));
    EXPECT_TRUE(RemoveGameplayTagUVE(tags, "undead"));
    EXPECT_FALSE(HasGameplayTagUVE(tags, "undead"));
    EXPECT_FALSE(RemoveGameplayTagUVE(tags, "undead"));
}

TEST(GameplayTagsUVETest, Add_RejectsBadTagsAndOverflow) {
    GameplayTagComponentUVE tags;
    EXPECT_FALSE(AddGameplayTagUVE(tags, ""));
    EXPECT_FALSE(AddGameplayTagUVE(tags, std::string(49U, 'x')));
    for (std::size_t index = 0U; index < kMaximumGameplayTagsUVE; ++index) {
        EXPECT_TRUE(AddGameplayTagUVE(tags, "tag" + std::to_string(index)));
    }
    EXPECT_FALSE(AddGameplayTagUVE(tags, "overflow"));
}

TEST(GameplayTagsUVETest, HasAllAndHasAny_MatchSetSemantics) {
    GameplayTagComponentUVE tags;
    ASSERT_TRUE(AddGameplayTagUVE(tags, "undead"));
    ASSERT_TRUE(AddGameplayTagUVE(tags, "boss"));
    EXPECT_TRUE(HasAllGameplayTagsUVE(tags, {"undead", "boss"}));
    EXPECT_FALSE(HasAllGameplayTagsUVE(tags, {"undead", "flammable"}));
    EXPECT_TRUE(HasAllGameplayTagsUVE(tags, {}));
    EXPECT_TRUE(HasAnyGameplayTagUVE(tags, {"flammable", "boss"}));
    EXPECT_FALSE(HasAnyGameplayTagUVE(tags, {"flammable", "flying"}));
    EXPECT_FALSE(HasAnyGameplayTagUVE(tags, {}));
}

TEST(GameplayTagsUVETest, Validity_RejectsDuplicatesBadTagsAndOverflow) {
    GameplayTagComponentUVE good;
    ASSERT_TRUE(AddGameplayTagUVE(good, "undead"));
    EXPECT_TRUE(IsGameplayTagComponentValidUVE(good));

    GameplayTagComponentUVE duplicate = good;
    duplicate.tags.push_back("undead");
    EXPECT_FALSE(IsGameplayTagComponentValidUVE(duplicate));

    GameplayTagComponentUVE empty;
    empty.tags.emplace_back("");
    EXPECT_FALSE(IsGameplayTagComponentValidUVE(empty));

    GameplayTagComponentUVE overfull;
    for (std::size_t index = 0U; index <= kMaximumGameplayTagsUVE; ++index) {
        overfull.tags.push_back("t" + std::to_string(index));
    }
    EXPECT_FALSE(IsGameplayTagComponentValidUVE(overfull));
}

} // namespace UVE::Scene::Tests
