// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/physics_islands_uve.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] Scene::EntityUVE MakeEntityUVE(const std::uint32_t index, const std::uint32_t generation = 0U) {
    Scene::EntityUVE entity;
    entity.index = index;
    entity.generation = generation;
    return entity;
}

[[nodiscard]] CollisionPairUVE MakePairUVE(const Scene::EntityUVE first, const Scene::EntityUVE second) {
    CollisionPairUVE pair;
    pair.first = first;
    pair.second = second;
    pair.separationAxis = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    pair.penetrationDepth = 0.1F;
    return pair;
}

/// Every pair appears exactly once, and the order inside each island is ascending: that is the
/// property the solver relies on, so it is checked rather than assumed.
void ExpectValidPartitionUVE(const PhysicsIslandPartitionUVE& partition, const std::size_t expectedPairCount) {
    ASSERT_EQ(partition.pairOrderUVE.size(), expectedPairCount);
    std::vector<std::uint32_t> seen(expectedPairCount, 0U);
    std::uint32_t expectedFirst = 0U;
    for (const PhysicsIslandUVE& island : partition.islandsUVE) {
        EXPECT_EQ(island.firstPairUVE, expectedFirst);
        expectedFirst += island.pairCountUVE;
        std::uint32_t previous = 0U;
        for (std::uint32_t offset = 0U; offset < island.pairCountUVE; ++offset) {
            const std::uint32_t pairIndex = partition.pairOrderUVE[island.firstPairUVE + offset];
            ASSERT_LT(pairIndex, expectedPairCount);
            ++seen[pairIndex];
            if (offset > 0U) {
                EXPECT_LT(previous, pairIndex) << "an island's pairs must stay in input order";
            }
            previous = pairIndex;
        }
    }
    EXPECT_EQ(expectedFirst, expectedPairCount) << "the islands must tile the pair list exactly";
    for (const std::uint32_t count : seen) {
        EXPECT_EQ(count, 1U) << "every pair must appear exactly once";
    }
}

} // namespace

TEST(PhysicsIslandsUVETest, EmptyInputProducesNoIslands) {
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE({});
    EXPECT_TRUE(partition.IsEmptyUVE());
    EXPECT_TRUE(partition.pairOrderUVE.empty());
    EXPECT_EQ(partition.bodyCountUVE, 0U);
}

TEST(PhysicsIslandsUVETest, OnePairIsOneIslandWithBothBodies) {
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(2U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    ASSERT_EQ(partition.islandsUVE.size(), 1U);
    EXPECT_EQ(partition.islandsUVE[0].pairCountUVE, 1U);
    EXPECT_EQ(partition.islandsUVE[0].bodyCountUVE, 2U);
    EXPECT_EQ(partition.bodyCountUVE, 2U);
    ExpectValidPartitionUVE(partition, 1U);
}

TEST(PhysicsIslandsUVETest, DisjointPairsProduceSeparateIslands) {
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(2U)),
                                              MakePairUVE(MakeEntityUVE(3U), MakeEntityUVE(4U)),
                                              MakePairUVE(MakeEntityUVE(5U), MakeEntityUVE(6U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    ASSERT_EQ(partition.islandsUVE.size(), 3U);
    for (const PhysicsIslandUVE& island : partition.islandsUVE) {
        EXPECT_EQ(island.pairCountUVE, 1U);
        EXPECT_EQ(island.bodyCountUVE, 2U);
    }
    EXPECT_EQ(partition.bodyCountUVE, 6U);
    ExpectValidPartitionUVE(partition, 3U);
}

TEST(PhysicsIslandsUVETest, AChainOfTouchingBodiesIsOneIsland) {
    // 1-2, 2-3, 3-4: the middle pairs are what join them. This is a stack of crates, and it must
    // not be split: every body in the chain can be affected by every other through the sequence.
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(2U)),
                                              MakePairUVE(MakeEntityUVE(2U), MakeEntityUVE(3U)),
                                              MakePairUVE(MakeEntityUVE(3U), MakeEntityUVE(4U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    ASSERT_EQ(partition.islandsUVE.size(), 1U);
    EXPECT_EQ(partition.islandsUVE[0].pairCountUVE, 3U);
    EXPECT_EQ(partition.islandsUVE[0].bodyCountUVE, 4U);
    ExpectValidPartitionUVE(partition, 3U);
}

TEST(PhysicsIslandsUVETest, IslandsAreNumberedByFirstAppearanceNotByLowestEntityIndex) {
    // The second island owns the lowest entity indices. Numbering by first appearance - not by
    // entity id - is what keeps the numbering a function of the pair list alone.
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(MakeEntityUVE(50U), MakeEntityUVE(51U)),
                                              MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(2U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    ASSERT_EQ(partition.islandsUVE.size(), 2U);
    EXPECT_EQ(partition.pairOrderUVE[0], 0U);
    EXPECT_EQ(partition.pairOrderUVE[1], 1U);
    EXPECT_EQ(partition.islandsUVE[0].pairCountUVE, 1U);
    EXPECT_EQ(partition.islandsUVE[1].pairCountUVE, 1U);
}

TEST(PhysicsIslandsUVETest, InterleavedIslandsKeepEveryPairInItsOriginalRelativeOrder) {
    // Island A: pairs 0 and 2. Island B: pair 1. The result must group them without reordering
    // either island internally, because the solver's arithmetic depends on that order.
    const Scene::EntityUVE first = MakeEntityUVE(1U);
    const Scene::EntityUVE second = MakeEntityUVE(2U);
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(first, second),
                                              MakePairUVE(MakeEntityUVE(3U), MakeEntityUVE(4U)),
                                              MakePairUVE(second, MakeEntityUVE(5U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    // The third pair joins `second`'s island, but the 3-4 pair stays on its own: two islands.
    ASSERT_EQ(partition.islandsUVE.size(), 2U);
    ExpectValidPartitionUVE(partition, 3U);

    // A genuinely interleaved case: A-B, C-D, A-B again. The repeated pair must stay second inside
    // island 0, and the islands' pairs must be grouped without touching that order.
    const std::vector<CollisionPairUVE> interleaved{MakePairUVE(MakeEntityUVE(10U), MakeEntityUVE(11U)),
                                                    MakePairUVE(MakeEntityUVE(20U), MakeEntityUVE(21U)),
                                                    MakePairUVE(MakeEntityUVE(10U), MakeEntityUVE(11U))};
    const PhysicsIslandPartitionUVE split = BuildPhysicsIslandPartitionUVE(interleaved);
    ASSERT_EQ(split.islandsUVE.size(), 2U);
    ASSERT_EQ(split.pairOrderUVE.size(), 3U);
    EXPECT_EQ(split.pairOrderUVE[0], 0U);
    EXPECT_EQ(split.pairOrderUVE[1], 2U);
    EXPECT_EQ(split.pairOrderUVE[2], 1U);
    EXPECT_EQ(split.islandsUVE[0].pairCountUVE, 2U);
    EXPECT_EQ(split.islandsUVE[1].pairCountUVE, 1U);
}

TEST(PhysicsIslandsUVETest, AStaticBodyThatTouchesTwoOthersJoinsThem) {
    // Nothing about the island depends on which side can move: a shared static collider still
    // means both dynamic bodies are solved against the same penetration data.
    const Scene::EntityUVE floor = MakeEntityUVE(100U);
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(MakeEntityUVE(1U), floor),
                                              MakePairUVE(floor, MakeEntityUVE(2U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    ASSERT_EQ(partition.islandsUVE.size(), 1U);
    EXPECT_EQ(partition.islandsUVE[0].bodyCountUVE, 3U);
}

TEST(PhysicsIslandsUVETest, SelfPairsAreIgnoredEntirely) {
    const Scene::EntityUVE body = MakeEntityUVE(7U);
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(body, body)};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    EXPECT_TRUE(partition.IsEmptyUVE());
    EXPECT_TRUE(partition.pairOrderUVE.empty());
}

TEST(PhysicsIslandsUVETest, ASelfPairDoesNotDeleteTheOtherPairs) {
    const Scene::EntityUVE body = MakeEntityUVE(7U);
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(body, body),
                                              MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(2U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    ASSERT_EQ(partition.islandsUVE.size(), 1U);
    ASSERT_EQ(partition.pairOrderUVE.size(), 1U);
    EXPECT_EQ(partition.pairOrderUVE[0], 1U) << "the surviving pair keeps its ORIGINAL index";
}

TEST(PhysicsIslandsUVETest, TheSameIndexWithADifferentGenerationIsADifferentBody) {
    // Entity indices are recycled. Treating a stale handle as the same body would fuse two
    // unrelated islands and serialize work that could have run concurrently - or worse, resolve a
    // pair against a body that no longer exists.
    const std::vector<CollisionPairUVE> pairs{MakePairUVE(MakeEntityUVE(9U, 0U), MakeEntityUVE(10U, 0U)),
                                              MakePairUVE(MakeEntityUVE(9U, 1U), MakeEntityUVE(11U, 0U))};
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    EXPECT_EQ(partition.islandsUVE.size(), 2U);
    ExpectValidPartitionUVE(partition, 2U);
}

TEST(PhysicsIslandsUVETest, EveryPairSurvivesARepeatedMergeOfTheSameIsland) {
    // A fully connected cluster given in an arbitrary order: the merge must end with one island
    // that still contains every pair exactly once, not a tree of overlapping groups.
    std::vector<CollisionPairUVE> pairs;
    constexpr std::uint32_t kBodyCount = 12U;
    for (std::uint32_t first = 0U; first < kBodyCount; ++first) {
        for (std::uint32_t second = first + 1U; second < kBodyCount; ++second) {
            pairs.push_back(MakePairUVE(MakeEntityUVE(first), MakeEntityUVE(second)));
        }
    }
    const PhysicsIslandPartitionUVE partition = BuildPhysicsIslandPartitionUVE(pairs);

    ASSERT_EQ(partition.islandsUVE.size(), 1U);
    EXPECT_EQ(partition.islandsUVE[0].bodyCountUVE, kBodyCount);
    ExpectValidPartitionUVE(partition, pairs.size());
}

} // namespace UVE::Physics::Tests
