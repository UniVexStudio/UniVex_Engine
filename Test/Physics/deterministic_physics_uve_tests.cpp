// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/physics/deterministic_physics_uve.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Physics::Tests {
namespace {

[[nodiscard]] Scene::EntityUVE MakeEntityUVE(const std::uint32_t index,
                                             const std::uint32_t generation = 0U) {
    Scene::EntityUVE entity{};
    entity.index = index;
    entity.generation = generation;
    return entity;
}

[[nodiscard]] DeterministicBodySampleUVE MakeSampleUVE(const Scene::EntityUVE entity,
                                                       const Math::Vector3UVE& position) {
    DeterministicBodySampleUVE sample{};
    sample.entity = entity;
    sample.position = position;
    sample.rotation = Math::QuaternionUVE{};
    sample.velocity = Math::Vector3UVE{};
    sample.angularVelocity = Math::Vector3UVE{};
    return sample;
}

[[nodiscard]] DeterministicStepSettingsUVE MakeEnabledSettingsUVE() {
    DeterministicStepSettingsUVE settings;
    settings.enabled = true;
    return settings;
}

// ---------------------------------------------------------------------------------------------
// Lattice
// ---------------------------------------------------------------------------------------------

TEST(DeterministicPhysicsUVETest, Lattice_RoundsToTheNearestGridPoint) {
    constexpr float quantum = 0.25F;
    EXPECT_EQ(QuantizeToLatticeUVE(1.0F, quantum), 4);
    EXPECT_EQ(QuantizeToLatticeUVE(1.1F, quantum), 4);
    EXPECT_EQ(QuantizeToLatticeUVE(1.2F, quantum), 5);
    EXPECT_EQ(QuantizeToLatticeUVE(-1.2F, quantum), -5);
    EXPECT_EQ(QuantizeToLatticeUVE(0.0F, quantum), 0);
}

TEST(DeterministicPhysicsUVETest, Lattice_RoundTripsExactly) {
    constexpr float quantum = 1.0F / 1024.0F;
    for (const float value : {0.0F, 1.0F, -1.0F, 12.5F, -0.001F, 1000.0F}) {
        const std::int64_t lattice = QuantizeToLatticeUVE(value, quantum);
        const float restored = LatticeToScalarUVE(lattice, quantum);
        // Re-quantizing the restored value must land on the same lattice point: a round trip that
        // does not is drift, and drift is what makes two peers disagree a minute later.
        EXPECT_EQ(QuantizeToLatticeUVE(restored, quantum), lattice) << "value " << value;
        EXPECT_NEAR(restored, value, quantum * 0.5F + 1.0e-6F);
    }
}

TEST(DeterministicPhysicsUVETest, Lattice_RejectsNonFiniteValuesAndQuanta) {
    constexpr float quantum = 1.0F / 1024.0F;
    EXPECT_EQ(QuantizeToLatticeUVE(std::numeric_limits<float>::quiet_NaN(), quantum), 0);
    EXPECT_EQ(QuantizeToLatticeUVE(std::numeric_limits<float>::infinity(), quantum), 0);
    EXPECT_EQ(QuantizeToLatticeUVE(-std::numeric_limits<float>::infinity(), quantum), 0);
    // A zero or negative quantum has no lattice; reporting zero is honest, wrapping is not.
    EXPECT_EQ(QuantizeToLatticeUVE(1.0F, 0.0F), 0);
    EXPECT_EQ(QuantizeToLatticeUVE(1.0F, -1.0F), 0);
    EXPECT_FLOAT_EQ(LatticeToScalarUVE(10, 0.0F), 0.0F);
}

TEST(DeterministicPhysicsUVETest, Lattice_AbsorbsSubQuantumDifferences) {
    // The property the digest relies on: two values closer than half a quantum map to one point.
    constexpr float quantum = 1.0F / 1024.0F;
    const float base = 3.0F;
    for (const float delta : {1.0e-7F, -1.0e-7F, quantum * 0.1F, -quantum * 0.1F}) {
        EXPECT_EQ(QuantizeToLatticeUVE(base + delta, quantum), QuantizeToLatticeUVE(base, quantum))
            << "delta " << delta;
    }
}

// ---------------------------------------------------------------------------------------------
// Body ordering
// ---------------------------------------------------------------------------------------------

TEST(DeterministicPhysicsUVETest, BodyKey_OrdersByIndexThenGeneration) {
    EXPECT_TRUE(MakeBodyKeyUVE(MakeEntityUVE(1U, 0U)) < MakeBodyKeyUVE(MakeEntityUVE(2U, 0U)));
    EXPECT_TRUE(MakeBodyKeyUVE(MakeEntityUVE(1U, 0U)) < MakeBodyKeyUVE(MakeEntityUVE(1U, 1U)));
    EXPECT_FALSE(MakeBodyKeyUVE(MakeEntityUVE(1U, 1U)) < MakeBodyKeyUVE(MakeEntityUVE(1U, 1U)));
    // A recycled index must not sort as "the same body" as the one that used to live there.
    EXPECT_TRUE(MakeBodyKeyUVE(MakeEntityUVE(1U, 0U)) != MakeBodyKeyUVE(MakeEntityUVE(1U, 5U)));
}

// ---------------------------------------------------------------------------------------------
// World digest
// ---------------------------------------------------------------------------------------------

TEST(DeterministicPhysicsUVETest, Digest_IsIndependentOfSampleOrder) {
    const DeterministicStepSettingsUVE settings = MakeEnabledSettingsUVE();
    const std::vector<DeterministicBodySampleUVE> forward{
        MakeSampleUVE(MakeEntityUVE(1U), Math::Vector3UVE{1.0F, 0.0F, 0.0F}),
        MakeSampleUVE(MakeEntityUVE(2U), Math::Vector3UVE{0.0F, 2.0F, 0.0F}),
        MakeSampleUVE(MakeEntityUVE(3U), Math::Vector3UVE{0.0F, 0.0F, 3.0F})};
    const std::vector<DeterministicBodySampleUVE> reversed{forward[2], forward[1], forward[0]};

    // This is the property that lets two peers digest the same world while discovering their
    // bodies in whatever order their storage happens to yield.
    EXPECT_EQ(ComputeWorldDigestUVE(forward, settings), ComputeWorldDigestUVE(reversed, settings));
}

TEST(DeterministicPhysicsUVETest, Digest_ChangesWhenAnyBodyActuallyMoves) {
    const DeterministicStepSettingsUVE settings = MakeEnabledSettingsUVE();
    const std::vector<DeterministicBodySampleUVE> before{
        MakeSampleUVE(MakeEntityUVE(1U), Math::Vector3UVE{1.0F, 0.0F, 0.0F})};
    std::vector<DeterministicBodySampleUVE> after = before;
    after[0].position.x += 0.5F;

    EXPECT_NE(ComputeWorldDigestUVE(before, settings), ComputeWorldDigestUVE(after, settings));
}

TEST(DeterministicPhysicsUVETest, Digest_IgnoresMotionBelowTheQuantum) {
    const DeterministicStepSettingsUVE settings = MakeEnabledSettingsUVE();
    const std::vector<DeterministicBodySampleUVE> before{
        MakeSampleUVE(MakeEntityUVE(1U), Math::Vector3UVE{1.0F, 0.0F, 0.0F})};
    std::vector<DeterministicBodySampleUVE> jittered = before;
    jittered[0].position.x += settings.positionQuantum * 0.2F;

    EXPECT_EQ(ComputeWorldDigestUVE(before, settings), ComputeWorldDigestUVE(jittered, settings));
}

TEST(DeterministicPhysicsUVETest, Digest_DistinguishesBodyCountAndIdentity) {
    const DeterministicStepSettingsUVE settings = MakeEnabledSettingsUVE();
    const std::vector<DeterministicBodySampleUVE> one{
        MakeSampleUVE(MakeEntityUVE(1U), Math::Vector3UVE{})};
    const std::vector<DeterministicBodySampleUVE> two{
        MakeSampleUVE(MakeEntityUVE(1U), Math::Vector3UVE{}),
        MakeSampleUVE(MakeEntityUVE(2U), Math::Vector3UVE{})};
    const std::vector<DeterministicBodySampleUVE> recycled{
        MakeSampleUVE(MakeEntityUVE(1U, 7U), Math::Vector3UVE{})};

    EXPECT_NE(ComputeWorldDigestUVE(one, settings), ComputeWorldDigestUVE(two, settings));
    // Same index, different generation: a different body, so a different digest.
    EXPECT_NE(ComputeWorldDigestUVE(one, settings), ComputeWorldDigestUVE(recycled, settings));
}

TEST(DeterministicPhysicsUVETest, Digest_DistinguishesVelocityEvenAtIdenticalPositions) {
    const DeterministicStepSettingsUVE settings = MakeEnabledSettingsUVE();
    std::vector<DeterministicBodySampleUVE> moving{
        MakeSampleUVE(MakeEntityUVE(1U), Math::Vector3UVE{})};
    moving[0].velocity = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    const std::vector<DeterministicBodySampleUVE> still{
        MakeSampleUVE(MakeEntityUVE(1U), Math::Vector3UVE{})};

    EXPECT_NE(ComputeWorldDigestUVE(moving, settings), ComputeWorldDigestUVE(still, settings));
}

TEST(DeterministicPhysicsUVETest, Digest_OfAnEmptyWorldIsStable) {
    const DeterministicStepSettingsUVE settings = MakeEnabledSettingsUVE();
    const std::vector<DeterministicBodySampleUVE> empty;
    EXPECT_EQ(ComputeWorldDigestUVE(empty, settings), ComputeWorldDigestUVE(empty, settings));
    EXPECT_NE(ComputeWorldDigestUVE(empty, settings), kFnvOffsetBasis64UVE);
}

// ---------------------------------------------------------------------------------------------
// Collision pair canonicalization
// ---------------------------------------------------------------------------------------------

[[nodiscard]] CollisionPairUVE MakePairUVE(const Scene::EntityUVE first, const Scene::EntityUVE second,
                                           const Math::Vector3UVE& axis, const float depth = 0.1F) {
    CollisionPairUVE pair{};
    pair.first = first;
    pair.second = second;
    pair.separationAxis = axis;
    pair.penetrationDepth = depth;
    return pair;
}

TEST(DeterministicPhysicsUVETest, Canonicalize_SortsPairsByStableKey) {
    std::vector<CollisionPairUVE> pairs{
        MakePairUVE(MakeEntityUVE(5U), MakeEntityUVE(9U), Math::Vector3UVE{1.0F, 0.0F, 0.0F}),
        MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(2U), Math::Vector3UVE{1.0F, 0.0F, 0.0F}),
        MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(7U), Math::Vector3UVE{1.0F, 0.0F, 0.0F})};

    CanonicalizeCollisionPairsUVE(pairs);

    ASSERT_EQ(pairs.size(), 3U);
    EXPECT_EQ(pairs[0].first, MakeEntityUVE(1U));
    EXPECT_EQ(pairs[0].second, MakeEntityUVE(2U));
    EXPECT_EQ(pairs[1].first, MakeEntityUVE(1U));
    EXPECT_EQ(pairs[1].second, MakeEntityUVE(7U));
    EXPECT_EQ(pairs[2].first, MakeEntityUVE(5U));
    EXPECT_EQ(pairs[2].second, MakeEntityUVE(9U));
}

TEST(DeterministicPhysicsUVETest, Canonicalize_OrientsEveryPairLowestKeyFirst) {
    // Handed in backwards on purpose: `separationAxis` points first -> second, so a swap that does
    // not negate the axis would resolve the pair in the wrong direction and push the bodies deeper.
    std::vector<CollisionPairUVE> pairs{
        MakePairUVE(MakeEntityUVE(9U), MakeEntityUVE(2U), Math::Vector3UVE{0.0F, 1.0F, 0.0F})};

    CanonicalizeCollisionPairsUVE(pairs);

    ASSERT_EQ(pairs.size(), 1U);
    EXPECT_EQ(pairs[0].first, MakeEntityUVE(2U));
    EXPECT_EQ(pairs[0].second, MakeEntityUVE(9U));
    EXPECT_FLOAT_EQ(pairs[0].separationAxis.x, 0.0F);
    EXPECT_FLOAT_EQ(pairs[0].separationAxis.y, -1.0F);
    // The depth is a scalar magnitude and must survive the swap untouched.
    EXPECT_FLOAT_EQ(pairs[0].penetrationDepth, 0.1F);
}

TEST(DeterministicPhysicsUVETest, Canonicalize_IsIdempotentAndOrderIndependent) {
    const std::vector<CollisionPairUVE> original{
        MakePairUVE(MakeEntityUVE(9U), MakeEntityUVE(2U), Math::Vector3UVE{0.0F, 1.0F, 0.0F}),
        MakePairUVE(MakeEntityUVE(3U), MakeEntityUVE(4U), Math::Vector3UVE{1.0F, 0.0F, 0.0F}),
        MakePairUVE(MakeEntityUVE(1U), MakeEntityUVE(8U), Math::Vector3UVE{1.0F, 0.0F, 0.0F})};
    std::vector<CollisionPairUVE> a = original;
    std::vector<CollisionPairUVE> b{original[2], original[0], original[1]};

    CanonicalizeCollisionPairsUVE(a);
    CanonicalizeCollisionPairsUVE(b);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t index = 0; index < a.size(); ++index) {
        EXPECT_EQ(a[index].first, b[index].first) << "index " << index;
        EXPECT_EQ(a[index].second, b[index].second) << "index " << index;
        EXPECT_EQ(a[index].separationAxis, b[index].separationAxis) << "index " << index;
    }

    // Running it again on an already-canonical list must not flip anything back.
    const std::vector<CollisionPairUVE> before = a;
    CanonicalizeCollisionPairsUVE(a);
    for (std::size_t index = 0; index < a.size(); ++index) {
        EXPECT_EQ(a[index].separationAxis, before[index].separationAxis) << "index " << index;
    }
}

TEST(DeterministicPhysicsUVETest, Canonicalize_LeavesAnEmptyListAlone) {
    std::vector<CollisionPairUVE> pairs;
    CanonicalizeCollisionPairsUVE(pairs);
    EXPECT_TRUE(pairs.empty());
}

} // namespace
} // namespace UVE::Physics::Tests
