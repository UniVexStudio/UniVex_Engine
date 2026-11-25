// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "deterministic_physics.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] Scene::Entity MakeEntity(const std::uint32_t index,
                                             const std::uint32_t generation = 0U) {
    Scene::Entity entity{};
    entity.index = index;
    entity.generation = generation;
    return entity;
}

[[nodiscard]] DeterministicBodySample MakeSample(const Scene::Entity entity,
                                                       const Math::Vector3& position) {
    DeterministicBodySample sample{};
    sample.entity = entity;
    sample.position = position;
    sample.rotation = Math::Quaternion{};
    sample.velocity = Math::Vector3{};
    sample.angularVelocity = Math::Vector3{};
    return sample;
}

[[nodiscard]] DeterministicStepSettings MakeEnabledSettings() {
    DeterministicStepSettings settings;
    settings.enabled = true;
    return settings;
}

// ---------------------------------------------------------------------------------------------
// Lattice
// ---------------------------------------------------------------------------------------------

TEST(DeterministicPhysicsTest, Lattice_RoundsToTheNearestGridPoint) {
    constexpr float quantum = 0.25F;
    EXPECT_EQ(QuantizeToLattice(1.0F, quantum), 4);
    EXPECT_EQ(QuantizeToLattice(1.1F, quantum), 4);
    EXPECT_EQ(QuantizeToLattice(1.2F, quantum), 5);
    EXPECT_EQ(QuantizeToLattice(-1.2F, quantum), -5);
    EXPECT_EQ(QuantizeToLattice(0.0F, quantum), 0);
}

TEST(DeterministicPhysicsTest, Lattice_RoundTripsExactly) {
    constexpr float quantum = 1.0F / 1024.0F;
    for (const float value : {0.0F, 1.0F, -1.0F, 12.5F, -0.001F, 1000.0F}) {
        const std::int64_t lattice = QuantizeToLattice(value, quantum);
        const float restored = LatticeToScalar(lattice, quantum);
        // Re-quantizing the restored value must land on the same lattice point: a round trip that
        // does not is drift, and drift is what makes two peers disagree a minute later.
        EXPECT_EQ(QuantizeToLattice(restored, quantum), lattice) << "value " << value;
        EXPECT_NEAR(restored, value, quantum * 0.5F + 1.0e-6F);
    }
}

TEST(DeterministicPhysicsTest, Lattice_RejectsNonFiniteValuesAndQuanta) {
    constexpr float quantum = 1.0F / 1024.0F;
    EXPECT_EQ(QuantizeToLattice(std::numeric_limits<float>::quiet_NaN(), quantum), 0);
    EXPECT_EQ(QuantizeToLattice(std::numeric_limits<float>::infinity(), quantum), 0);
    EXPECT_EQ(QuantizeToLattice(-std::numeric_limits<float>::infinity(), quantum), 0);
    // A zero or negative quantum has no lattice; reporting zero is honest, wrapping is not.
    EXPECT_EQ(QuantizeToLattice(1.0F, 0.0F), 0);
    EXPECT_EQ(QuantizeToLattice(1.0F, -1.0F), 0);
    EXPECT_FLOAT_EQ(LatticeToScalar(10, 0.0F), 0.0F);
}

TEST(DeterministicPhysicsTest, Lattice_AbsorbsSubQuantumDifferences) {
    // The property the digest relies on: two values closer than half a quantum map to one point.
    constexpr float quantum = 1.0F / 1024.0F;
    const float base = 3.0F;
    for (const float delta : {1.0e-7F, -1.0e-7F, quantum * 0.1F, -quantum * 0.1F}) {
        EXPECT_EQ(QuantizeToLattice(base + delta, quantum), QuantizeToLattice(base, quantum))
            << "delta " << delta;
    }
}

// ---------------------------------------------------------------------------------------------
// Body ordering
// ---------------------------------------------------------------------------------------------

TEST(DeterministicPhysicsTest, BodyKey_OrdersByIndexThenGeneration) {
    EXPECT_TRUE(MakeBodyKey(MakeEntity(1U, 0U)) < MakeBodyKey(MakeEntity(2U, 0U)));
    EXPECT_TRUE(MakeBodyKey(MakeEntity(1U, 0U)) < MakeBodyKey(MakeEntity(1U, 1U)));
    EXPECT_FALSE(MakeBodyKey(MakeEntity(1U, 1U)) < MakeBodyKey(MakeEntity(1U, 1U)));
    // A recycled index must not sort as "the same body" as the one that used to live there.
    EXPECT_TRUE(MakeBodyKey(MakeEntity(1U, 0U)) != MakeBodyKey(MakeEntity(1U, 5U)));
}

// ---------------------------------------------------------------------------------------------
// World digest
// ---------------------------------------------------------------------------------------------

TEST(DeterministicPhysicsTest, Digest_IsIndependentOfSampleOrder) {
    const DeterministicStepSettings settings = MakeEnabledSettings();
    const std::vector<DeterministicBodySample> forward{
        MakeSample(MakeEntity(1U), Math::Vector3{1.0F, 0.0F, 0.0F}),
        MakeSample(MakeEntity(2U), Math::Vector3{0.0F, 2.0F, 0.0F}),
        MakeSample(MakeEntity(3U), Math::Vector3{0.0F, 0.0F, 3.0F})};
    const std::vector<DeterministicBodySample> reversed{forward[2], forward[1], forward[0]};

    // This is the property that lets two peers digest the same world while discovering their
    // bodies in whatever order their storage happens to yield.
    EXPECT_EQ(ComputeWorldDigest(forward, settings), ComputeWorldDigest(reversed, settings));
}

TEST(DeterministicPhysicsTest, Digest_ChangesWhenAnyBodyActuallyMoves) {
    const DeterministicStepSettings settings = MakeEnabledSettings();
    const std::vector<DeterministicBodySample> before{
        MakeSample(MakeEntity(1U), Math::Vector3{1.0F, 0.0F, 0.0F})};
    std::vector<DeterministicBodySample> after = before;
    after[0].position.x += 0.5F;

    EXPECT_NE(ComputeWorldDigest(before, settings), ComputeWorldDigest(after, settings));
}

TEST(DeterministicPhysicsTest, Digest_IgnoresMotionBelowTheQuantum) {
    const DeterministicStepSettings settings = MakeEnabledSettings();
    const std::vector<DeterministicBodySample> before{
        MakeSample(MakeEntity(1U), Math::Vector3{1.0F, 0.0F, 0.0F})};
    std::vector<DeterministicBodySample> jittered = before;
    jittered[0].position.x += settings.positionQuantum * 0.2F;

    EXPECT_EQ(ComputeWorldDigest(before, settings), ComputeWorldDigest(jittered, settings));
}

TEST(DeterministicPhysicsTest, Digest_DistinguishesBodyCountAndIdentity) {
    const DeterministicStepSettings settings = MakeEnabledSettings();
    const std::vector<DeterministicBodySample> one{
        MakeSample(MakeEntity(1U), Math::Vector3{})};
    const std::vector<DeterministicBodySample> two{
        MakeSample(MakeEntity(1U), Math::Vector3{}),
        MakeSample(MakeEntity(2U), Math::Vector3{})};
    const std::vector<DeterministicBodySample> recycled{
        MakeSample(MakeEntity(1U, 7U), Math::Vector3{})};

    EXPECT_NE(ComputeWorldDigest(one, settings), ComputeWorldDigest(two, settings));
    // Same index, different generation: a different body, so a different digest.
    EXPECT_NE(ComputeWorldDigest(one, settings), ComputeWorldDigest(recycled, settings));
}

TEST(DeterministicPhysicsTest, Digest_DistinguishesVelocityEvenAtIdenticalPositions) {
    const DeterministicStepSettings settings = MakeEnabledSettings();
    std::vector<DeterministicBodySample> moving{
        MakeSample(MakeEntity(1U), Math::Vector3{})};
    moving[0].velocity = Math::Vector3{5.0F, 0.0F, 0.0F};
    const std::vector<DeterministicBodySample> still{
        MakeSample(MakeEntity(1U), Math::Vector3{})};

    EXPECT_NE(ComputeWorldDigest(moving, settings), ComputeWorldDigest(still, settings));
}

TEST(DeterministicPhysicsTest, Digest_OfAnEmptyWorldIsStable) {
    const DeterministicStepSettings settings = MakeEnabledSettings();
    const std::vector<DeterministicBodySample> empty;
    EXPECT_EQ(ComputeWorldDigest(empty, settings), ComputeWorldDigest(empty, settings));
    EXPECT_NE(ComputeWorldDigest(empty, settings), kFnvOffsetBasis64);
}

// ---------------------------------------------------------------------------------------------
// Collision pair canonicalization
// ---------------------------------------------------------------------------------------------

[[nodiscard]] CollisionPair MakePair(const Scene::Entity first, const Scene::Entity second,
                                           const Math::Vector3& axis, const float depth = 0.1F) {
    CollisionPair pair{};
    pair.first = first;
    pair.second = second;
    pair.separationAxis = axis;
    pair.penetrationDepth = depth;
    return pair;
}

TEST(DeterministicPhysicsTest, Canonicalize_SortsPairsByStableKey) {
    std::vector<CollisionPair> pairs{
        MakePair(MakeEntity(5U), MakeEntity(9U), Math::Vector3{1.0F, 0.0F, 0.0F}),
        MakePair(MakeEntity(1U), MakeEntity(2U), Math::Vector3{1.0F, 0.0F, 0.0F}),
        MakePair(MakeEntity(1U), MakeEntity(7U), Math::Vector3{1.0F, 0.0F, 0.0F})};

    CanonicalizeCollisionPairs(pairs);

    ASSERT_EQ(pairs.size(), 3U);
    EXPECT_EQ(pairs[0].first, MakeEntity(1U));
    EXPECT_EQ(pairs[0].second, MakeEntity(2U));
    EXPECT_EQ(pairs[1].first, MakeEntity(1U));
    EXPECT_EQ(pairs[1].second, MakeEntity(7U));
    EXPECT_EQ(pairs[2].first, MakeEntity(5U));
    EXPECT_EQ(pairs[2].second, MakeEntity(9U));
}

TEST(DeterministicPhysicsTest, Canonicalize_OrientsEveryPairLowestKeyFirst) {
    // Handed in backwards on purpose: `separationAxis` points first -> second, so a swap that does
    // not negate the axis would resolve the pair in the wrong direction and push the bodies deeper.
    std::vector<CollisionPair> pairs{
        MakePair(MakeEntity(9U), MakeEntity(2U), Math::Vector3{0.0F, 1.0F, 0.0F})};

    CanonicalizeCollisionPairs(pairs);

    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].first, MakeEntity(2U));
    EXPECT_EQ(pairs[0].second, MakeEntity(9U));
    EXPECT_FLOAT_EQ(pairs[0].separationAxis.x, 0.0F);
    EXPECT_FLOAT_EQ(pairs[0].separationAxis.y, -1.0F);
    // The depth is a scalar magnitude and must survive the swap untouched.
    EXPECT_FLOAT_EQ(pairs[0].penetrationDepth, 0.1F);
}

TEST(DeterministicPhysicsTest, Canonicalize_IsIdempotentAndOrderIndependent) {
    const std::vector<CollisionPair> original{
        MakePair(MakeEntity(9U), MakeEntity(2U), Math::Vector3{0.0F, 1.0F, 0.0F}),
        MakePair(MakeEntity(3U), MakeEntity(4U), Math::Vector3{1.0F, 0.0F, 0.0F}),
        MakePair(MakeEntity(1U), MakeEntity(8U), Math::Vector3{1.0F, 0.0F, 0.0F})};
    std::vector<CollisionPair> a = original;
    std::vector<CollisionPair> b{original[2], original[0], original[1]};

    CanonicalizeCollisionPairs(a);
    CanonicalizeCollisionPairs(b);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t index = 0; index < a.size(); ++index) {
        EXPECT_EQ(a[index].first, b[index].first) << "index " << index;
        EXPECT_EQ(a[index].second, b[index].second) << "index " << index;
        EXPECT_EQ(a[index].separationAxis, b[index].separationAxis) << "index " << index;
    }

    // Running it again on an already-canonical list must not flip anything back.
    const std::vector<CollisionPair> before = a;
    CanonicalizeCollisionPairs(a);
    for (std::size_t index = 0; index < a.size(); ++index) {
        EXPECT_EQ(a[index].separationAxis, before[index].separationAxis) << "index " << index;
    }
}

TEST(DeterministicPhysicsTest, Canonicalize_LeavesAnEmptyListAlone) {
    std::vector<CollisionPair> pairs;
    CanonicalizeCollisionPairs(pairs);
    EXPECT_TRUE(pairs.empty());
}

} // namespace
} // namespace UVE::Physics::Tests
