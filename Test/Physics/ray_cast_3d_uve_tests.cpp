// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/ray_cast_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

constexpr EntityUVE kRay{1U, 1U};
constexpr EntityUVE kOwner{2U, 1U};
constexpr EntityUVE kWall{3U, 1U};

TEST(RayCast3DUVETest, IsCastingUVE_FollowsEnabled) {
    RayCast3DComponentUVE rayCast{};
    EXPECT_TRUE(RayCast3DUVE::IsCastingUVE(rayCast));
    rayCast.enabled = false;
    EXPECT_FALSE(RayCast3DUVE::IsCastingUVE(rayCast));
}

TEST(RayCast3DUVETest, AcceptsTargetUVE_SkipsSelfAndExclusionPrefix) {
    RayCast3DComponentUVE rayCast{};
    EXPECT_TRUE(RayCast3DUVE::AcceptsTargetUVE(kRay, rayCast, kWall));
    EXPECT_FALSE(RayCast3DUVE::AcceptsTargetUVE(kRay, rayCast, kRay));
    EXPECT_FALSE(RayCast3DUVE::AcceptsTargetUVE(kRay, rayCast, kInvalidEntityUVE));

    rayCast.exclusions[0] = kOwner;
    EXPECT_EQ(RayCast3DUVE::ExclusionCountUVE(rayCast), 1U);
    EXPECT_EQ(RayCast3DUVE::ExclusionSpanUVE(rayCast).size(), 1U);
    EXPECT_FALSE(RayCast3DUVE::AcceptsTargetUVE(kRay, rayCast, kOwner));
    EXPECT_TRUE(RayCast3DUVE::AcceptsTargetUVE(kRay, rayCast, kWall));
}

TEST(RayCast3DUVETest, ResolveWorldDirectionUVE_IdentityKeepsLocalAndRotationTurnsIt) {
    const Math::Vector3UVE local{0.0F, -1.0F, 0.0F};
    const Math::Vector3UVE identity = RayCast3DUVE::ResolveWorldDirectionUVE(local, {});
    EXPECT_NEAR(identity.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(identity.y, -1.0F, 1.0e-5F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-5F);

    Math::QuaternionUVE quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Math::Vector3UVE turned = RayCast3DUVE::ResolveWorldDirectionUVE({1.0F, 0.0F, 0.0F}, quarterTurn);
    const Math::Vector3UVE expected = Math::RotateVectorUVE(quarterTurn, {1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(turned.x, expected.x, 1.0e-5F);
    EXPECT_NEAR(turned.y, expected.y, 1.0e-5F);
    EXPECT_NEAR(turned.z, expected.z, 1.0e-5F);
}

TEST(RayCast3DUVETest, ResolveWorldDirectionUVE_NonFiniteIsStanding) {
    const Math::Vector3UVE nan{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const Math::Vector3UVE world = RayCast3DUVE::ResolveWorldDirectionUVE(nan, {});
    EXPECT_FLOAT_EQ(world.x, 0.0F);
    EXPECT_FLOAT_EQ(world.y, 0.0F);
    EXPECT_FLOAT_EQ(world.z, 0.0F);
}

TEST(RayCast3DUVETest, ClearAndRecordHitUVE_WriteThePerFrameResult) {
    RayCast3DComponentUVE rayCast{};
    RayCast3DUVE::RecordHitUVE(rayCast, kWall, Math::Vector3UVE{1.0F, 2.0F, 3.0F},
                               Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    EXPECT_TRUE(rayCast.hit);
    EXPECT_EQ(rayCast.hitEntity, kWall);
    EXPECT_EQ(rayCast.hitPosition, (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(rayCast.hitNormal, (Math::Vector3UVE{0.0F, 1.0F, 0.0F}));

    RayCast3DUVE::ClearResultUVE(rayCast);
    EXPECT_FALSE(rayCast.hit);
    EXPECT_EQ(rayCast.hitEntity, kInvalidEntityUVE);
    EXPECT_EQ(rayCast.hitPosition, Math::Vector3UVE{});
    EXPECT_EQ(rayCast.hitNormal, Math::Vector3UVE{});
}

} // namespace
} // namespace UVE::Scene::Tests
