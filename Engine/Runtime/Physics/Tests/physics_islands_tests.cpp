// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "physics_islands.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] Scene::Entity MakeEntity(const std::uint32_t index, const std::uint32_t generation = 0U) {
    Scene::Entity entity;
    entity.index = index;
    entity.generation = generation;
    return entity;
}

[[nodiscard]] CollisionPair MakePair(const Scene::Entity first, const Scene::Entity second) {
    CollisionPair pair;
    pair.first = first;
    pair.second = second;
    pair.separationAxis = Math::Vector3{1.0F, 0.0F, 0.0F};
    pair.penetrationDepth = 0.1F;
    return pair;
}

/// Every pair appears exactly once, and the order inside each island is ascending: that is the
/// property the solver relies on, so it is checked rather than assumed.
void ExpectValidPartition(const PhysicsIslandPartition& partition, const std::size_t expectedPairCount) {
    ASSERT_EQ(partition.pairOrder.size(), expectedPairCount);
    std::vector<std::uint32_t> seen(expectedPairCount, 0U);
    std::uint32_t expectedFirst = 0U;
    for (const PhysicsIsland& island : partition.islands) {
        EXPECT_EQ(island.firstPair, expectedFirst);
        expectedFirst += island.pairCount;
        std::uint32_t previous = 0U;
        for (std::uint32_t offset = 0U; offset < island.pairCount; ++offset) {
            const std::uint32_t pairIndex = partition.pairOrder[island.firstPair + offset];
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

TEST(PhysicsIslandsTest, EmptyInputProducesNoIslands) {
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition({});
    EXPECT_TRUE(partition.IsEmpty());
    EXPECT_TRUE(partition.pairOrder.empty());
    EXPECT_EQ(partition.bodyCount, 0U);
}

TEST(PhysicsIslandsTest, OnePairIsOneIslandWithBothBodies) {
    const std::vector<CollisionPair> pairs{MakePair(MakeEntity(1U), MakeEntity(2U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    ASSERT_EQ(partition.islands.size(), 1U);
    EXPECT_EQ(partition.islands[0].pairCount, 1U);
    EXPECT_EQ(partition.islands[0].bodyCount, 2U);
    EXPECT_EQ(partition.bodyCount, 2U);
    ExpectValidPartition(partition, 1U);
}

TEST(PhysicsIslandsTest, DisjointPairsProduceSeparateIslands) {
    const std::vector<CollisionPair> pairs{MakePair(MakeEntity(1U), MakeEntity(2U)),
                                              MakePair(MakeEntity(3U), MakeEntity(4U)),
                                              MakePair(MakeEntity(5U), MakeEntity(6U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    ASSERT_EQ(partition.islands.size(), 3U);
    for (const PhysicsIsland& island : partition.islands) {
        EXPECT_EQ(island.pairCount, 1U);
        EXPECT_EQ(island.bodyCount, 2U);
    }
    EXPECT_EQ(partition.bodyCount, 6U);
    ExpectValidPartition(partition, 3U);
}

TEST(PhysicsIslandsTest, AChainOfTouchingBodiesIsOneIsland) {
    // 1-2, 2-3, 3-4: the middle pairs are what join them. This is a stack of crates, and it must
    // not be split: every body in the chain can be affected by every other through the sequence.
    const std::vector<CollisionPair> pairs{MakePair(MakeEntity(1U), MakeEntity(2U)),
                                              MakePair(MakeEntity(2U), MakeEntity(3U)),
                                              MakePair(MakeEntity(3U), MakeEntity(4U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    ASSERT_EQ(partition.islands.size(), 1U);
    EXPECT_EQ(partition.islands[0].pairCount, 3U);
    EXPECT_EQ(partition.islands[0].bodyCount, 4U);
    ExpectValidPartition(partition, 3U);
}

TEST(PhysicsIslandsTest, IslandsAreNumberedByFirstAppearanceNotByLowestEntityIndex) {
    // The second island owns the lowest entity indices. Numbering by first appearance - not by
    // entity id - is what keeps the numbering a function of the pair list alone.
    const std::vector<CollisionPair> pairs{MakePair(MakeEntity(50U), MakeEntity(51U)),
                                              MakePair(MakeEntity(1U), MakeEntity(2U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    ASSERT_EQ(partition.islands.size(), 2U);
    EXPECT_EQ(partition.pairOrder[0], 0U);
    EXPECT_EQ(partition.pairOrder[1], 1U);
    EXPECT_EQ(partition.islands[0].pairCount, 1U);
    EXPECT_EQ(partition.islands[1].pairCount, 1U);
}

TEST(PhysicsIslandsTest, InterleavedIslandsKeepEveryPairInItsOriginalRelativeOrder) {
    // Island A: pairs 0 and 2. Island B: pair 1. The result must group them without reordering
    // either island internally, because the solver's arithmetic depends on that order.
    const Scene::Entity first = MakeEntity(1U);
    const Scene::Entity second = MakeEntity(2U);
    const std::vector<CollisionPair> pairs{MakePair(first, second),
                                              MakePair(MakeEntity(3U), MakeEntity(4U)),
                                              MakePair(second, MakeEntity(5U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    // The third pair joins `second`'s island, but the 3-4 pair stays on its own: two islands.
    ASSERT_EQ(partition.islands.size(), 2U);
    ExpectValidPartition(partition, 3U);

    // A genuinely interleaved case: A-B, C-D, A-B again. The repeated pair must stay second inside
    // island 0, and the islands' pairs must be grouped without touching that order.
    const std::vector<CollisionPair> interleaved{MakePair(MakeEntity(10U), MakeEntity(11U)),
                                                    MakePair(MakeEntity(20U), MakeEntity(21U)),
                                                    MakePair(MakeEntity(10U), MakeEntity(11U))};
    const PhysicsIslandPartition split = BuildPhysicsIslandPartition(interleaved);
    ASSERT_EQ(split.islands.size(), 2U);
    ASSERT_EQ(split.pairOrder.size(), 3U);
    EXPECT_EQ(split.pairOrder[0], 0U);
    EXPECT_EQ(split.pairOrder[1], 2U);
    EXPECT_EQ(split.pairOrder[2], 1U);
    EXPECT_EQ(split.islands[0].pairCount, 2U);
    EXPECT_EQ(split.islands[1].pairCount, 1U);
}

TEST(PhysicsIslandsTest, AStaticBodyThatTouchesTwoOthersJoinsThem) {
    // Nothing about the island depends on which side can move: a shared static collider still
    // means both dynamic bodies are solved against the same penetration data.
    const Scene::Entity floor = MakeEntity(100U);
    const std::vector<CollisionPair> pairs{MakePair(MakeEntity(1U), floor),
                                              MakePair(floor, MakeEntity(2U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    ASSERT_EQ(partition.islands.size(), 1U);
    EXPECT_EQ(partition.islands[0].bodyCount, 3U);
}

TEST(PhysicsIslandsTest, StaticBodySetKeepsDynamicIslandsIndependent) {
    const Scene::Entity floor = MakeEntity(100U);
    const std::vector<CollisionPair> pairs{MakePair(MakeEntity(1U), floor),
                                              MakePair(floor, MakeEntity(2U))};
    const std::unordered_set<Scene::Entity> staticBodies{floor};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs, staticBodies);

    ASSERT_EQ(partition.islands.size(), 2U);
    EXPECT_EQ(partition.islands[0].pairCount, 1U);
    EXPECT_EQ(partition.islands[1].pairCount, 1U);
    ExpectValidPartition(partition, 2U);
}

TEST(PhysicsIslandsTest, SelfPairsAreIgnoredEntirely) {
    const Scene::Entity body = MakeEntity(7U);
    const std::vector<CollisionPair> pairs{MakePair(body, body)};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    EXPECT_TRUE(partition.IsEmpty());
    EXPECT_TRUE(partition.pairOrder.empty());
}

TEST(PhysicsIslandsTest, ASelfPairDoesNotDeleteTheOtherPairs) {
    const Scene::Entity body = MakeEntity(7U);
    const std::vector<CollisionPair> pairs{MakePair(body, body),
                                              MakePair(MakeEntity(1U), MakeEntity(2U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    ASSERT_EQ(partition.islands.size(), 1U);
    ASSERT_EQ(partition.pairOrder.size(), 1U);
    EXPECT_EQ(partition.pairOrder[0], 1U) << "the surviving pair keeps its ORIGINAL index";
}

TEST(PhysicsIslandsTest, TheSameIndexWithADifferentGenerationIsADifferentBody) {
    // Entity indices are recycled. Treating a stale handle as the same body would fuse two
    // unrelated islands and serialize work that could have run concurrently - or worse, resolve a
    // pair against a body that no longer exists.
    const std::vector<CollisionPair> pairs{MakePair(MakeEntity(9U, 0U), MakeEntity(10U, 0U)),
                                              MakePair(MakeEntity(9U, 1U), MakeEntity(11U, 0U))};
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    EXPECT_EQ(partition.islands.size(), 2U);
    ExpectValidPartition(partition, 2U);
}

TEST(PhysicsIslandsTest, EveryPairSurvivesARepeatedMergeOfTheSameIsland) {
    // A fully connected cluster given in an arbitrary order: the merge must end with one island
    // that still contains every pair exactly once, not a tree of overlapping groups.
    std::vector<CollisionPair> pairs;
    constexpr std::uint32_t kBodyCount = 12U;
    for (std::uint32_t first = 0U; first < kBodyCount; ++first) {
        for (std::uint32_t second = first + 1U; second < kBodyCount; ++second) {
            pairs.push_back(MakePair(MakeEntity(first), MakeEntity(second)));
        }
    }
    const PhysicsIslandPartition partition = BuildPhysicsIslandPartition(pairs);

    ASSERT_EQ(partition.islands.size(), 1U);
    EXPECT_EQ(partition.islands[0].bodyCount, kBodyCount);
    ExpectValidPartition(partition, pairs.size());
}

} // namespace UVE::Physics::Tests
