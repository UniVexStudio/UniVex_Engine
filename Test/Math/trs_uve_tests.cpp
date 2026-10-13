// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/trs_uve.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <string>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

TEST(TrsUVETest, DefaultConstruction_IsIdentity) {
    constexpr TrsUVE trs{};
    EXPECT_EQ(trs.translation, (Vector3UVE{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(trs.rotation, (QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(trs.scale, (Vector3UVE{1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(trs, TrsUVE::IdentityUVE());
}

TEST(TrsUVETest, EqualityOperators_CompareAllFields) {
    const TrsUVE a{Vector3UVE{1.0F, 2.0F, 3.0F}, QuaternionUVE{}, Vector3UVE{1.0F, 1.0F, 1.0F}};
    const TrsUVE b = a;
    const TrsUVE moved{Vector3UVE{9.0F, 2.0F, 3.0F}, QuaternionUVE{}, Vector3UVE{1.0F, 1.0F, 1.0F}};
    const TrsUVE turned{
        Vector3UVE{1.0F, 2.0F, 3.0F}, QuaternionUVE{0.0F, 0.0F, 1.0F, 0.0F}, Vector3UVE{1.0F, 1.0F, 1.0F}};
    const TrsUVE scaled{Vector3UVE{1.0F, 2.0F, 3.0F}, QuaternionUVE{}, Vector3UVE{2.0F, 1.0F, 1.0F}};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != moved);
    EXPECT_TRUE(a != turned);
    EXPECT_TRUE(a != scaled);
}

TEST(TrsUVETest, ComposeUVE_WithIdentity_IsUnchanged) {
    const TrsUVE trs{Vector3UVE{1.0F, 2.0F, 3.0F},
                     QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3UVE{2.0F, 3.0F, 4.0F}};

    EXPECT_EQ(ComposeUVE(TrsUVE::IdentityUVE(), trs), trs);
    EXPECT_EQ(ComposeUVE(trs, TrsUVE::IdentityUVE()), trs);
}

TEST(TrsUVETest, ComposeUVE_KnownTransforms_MatchesHandComputedValue) {
    // Parent: translated (1,0,0), rotated +90 degrees about Z, uniformly scaled by 2.
    const TrsUVE parent{Vector3UVE{1.0F, 0.0F, 0.0F},
                        QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                      std::numbers::sqrt2_v<float> / 2.0F},
                        Vector3UVE{2.0F, 2.0F, 2.0F}};
    // Child: translated (1,0,0), unrotated, uniformly scaled by 3.
    const TrsUVE child{Vector3UVE{1.0F, 0.0F, 0.0F}, QuaternionUVE{}, Vector3UVE{3.0F, 3.0F, 3.0F}};

    // scale = (6,6,6); rotation = parent's 90-degree Z; translation = (1,0,0) + Rz90*(2,0,0) = (1,2,0).
    const TrsUVE composed = ComposeUVE(parent, child);

    EXPECT_EQ(composed.scale, (Vector3UVE{6.0F, 6.0F, 6.0F}));
    EXPECT_NEAR(composed.rotation.x, 0.0F, kEpsilon);
    EXPECT_NEAR(composed.rotation.y, 0.0F, kEpsilon);
    EXPECT_NEAR(composed.rotation.z, std::numbers::sqrt2_v<float> / 2.0F, kEpsilon);
    EXPECT_NEAR(composed.rotation.w, std::numbers::sqrt2_v<float> / 2.0F, kEpsilon);
    EXPECT_NEAR(composed.translation.x, 1.0F, kEpsilon);
    EXPECT_NEAR(composed.translation.y, 2.0F, kEpsilon);
    EXPECT_NEAR(composed.translation.z, 0.0F, kEpsilon);
}

TEST(TrsUVETest, ComposeUVE_IsAssociative) {
    // Uniform scales: the domain associativity holds over (a uniform scale commutes past any
    // rotation, so grouping a chain cannot matter). Non-uniform scales under rotation are
    // order-sensitive by construction - see ComposeUVE's doc comment.
    const TrsUVE a{Vector3UVE{1.0F, 2.0F, 3.0F},
                   QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                 std::numbers::sqrt2_v<float> / 2.0F},
                   Vector3UVE{2.0F, 2.0F, 2.0F}};
    const TrsUVE b{Vector3UVE{-1.0F, 0.5F, 2.0F},
                   QuaternionUVE{std::numbers::sqrt2_v<float> / 2.0F, 0.0F, 0.0F,
                                 std::numbers::sqrt2_v<float> / 2.0F},
                   Vector3UVE{3.0F, 3.0F, 3.0F}};
    const TrsUVE c{Vector3UVE{0.0F, 0.0F, 1.0F}, QuaternionUVE{}, Vector3UVE{0.5F, 0.5F, 0.5F}};

    const TrsUVE leftFirst = ComposeUVE(ComposeUVE(a, b), c);
    const TrsUVE rightFirst = ComposeUVE(a, ComposeUVE(b, c));

    EXPECT_NEAR(leftFirst.translation.x, rightFirst.translation.x, kEpsilon);
    EXPECT_NEAR(leftFirst.translation.y, rightFirst.translation.y, kEpsilon);
    EXPECT_NEAR(leftFirst.translation.z, rightFirst.translation.z, kEpsilon);
    EXPECT_NEAR(leftFirst.rotation.x, rightFirst.rotation.x, kEpsilon);
    EXPECT_NEAR(leftFirst.rotation.y, rightFirst.rotation.y, kEpsilon);
    EXPECT_NEAR(leftFirst.rotation.z, rightFirst.rotation.z, kEpsilon);
    EXPECT_NEAR(leftFirst.rotation.w, rightFirst.rotation.w, kEpsilon);
    EXPECT_NEAR(leftFirst.scale.x, rightFirst.scale.x, kEpsilon);
    EXPECT_NEAR(leftFirst.scale.y, rightFirst.scale.y, kEpsilon);
    EXPECT_NEAR(leftFirst.scale.z, rightFirst.scale.z, kEpsilon);
}

TEST(TrsUVETest, TransformPointUVE_KnownTransform_MatchesHandComputedValue) {
    const TrsUVE trs{Vector3UVE{1.0F, 0.0F, 0.0F},
                     QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3UVE{2.0F, 2.0F, 2.0F}};

    // (1,0,0) + Rz90*((2,2,2) * (1,0,0)) = (1,0,0) + Rz90*(2,0,0) = (1,2,0).
    const Vector3UVE result = TransformPointUVE(trs, Vector3UVE{1.0F, 0.0F, 0.0F});

    EXPECT_NEAR(result.x, 1.0F, kEpsilon);
    EXPECT_NEAR(result.y, 2.0F, kEpsilon);
    EXPECT_NEAR(result.z, 0.0F, kEpsilon);
}

TEST(TrsUVETest, TransformDirectionUVE_IgnoresTranslation) {
    const TrsUVE moved{Vector3UVE{5.0F, 6.0F, 7.0F}, QuaternionUVE{}, Vector3UVE{2.0F, 3.0F, 4.0F}};
    const TrsUVE unmoved{Vector3UVE{}, QuaternionUVE{}, Vector3UVE{2.0F, 3.0F, 4.0F}};

    EXPECT_EQ(TransformDirectionUVE(moved, Vector3UVE{1.0F, 1.0F, 1.0F}),
              TransformDirectionUVE(unmoved, Vector3UVE{1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(TransformDirectionUVE(moved, Vector3UVE{1.0F, 1.0F, 1.0F}),
              (Vector3UVE{2.0F, 3.0F, 4.0F}));
}

TEST(TrsUVETest, InverseTransformPoint_RoundTripsThroughNonUniformRotatedParent) {
    // The case with NO materialized inverse TRS (rotation and non-uniform scale do not commute):
    // forward-then-inverse must still round-trip, which is why the inverse is offered as
    // application functions rather than as an inverse value.
    const TrsUVE trs{Vector3UVE{3.0F, -4.0F, 5.0F},
                     QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3UVE{2.0F, 3.0F, 4.0F}};
    const Vector3UVE point{1.0F, -2.0F, 3.0F};

    const Vector3UVE moved = TransformPointUVE(trs, point);
    Vector3UVE back{};

    ASSERT_TRUE(TryInverseTransformPointUVE(trs, moved, back));
    EXPECT_NEAR(back.x, point.x, kEpsilon);
    EXPECT_NEAR(back.y, point.y, kEpsilon);
    EXPECT_NEAR(back.z, point.z, kEpsilon);
}

TEST(TrsUVETest, InverseTransformDirection_RoundTrips) {
    const TrsUVE trs{Vector3UVE{3.0F, -4.0F, 5.0F},
                     QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3UVE{2.0F, 3.0F, 4.0F}};
    const Vector3UVE direction{1.0F, 1.0F, 1.0F};

    const Vector3UVE moved = TransformDirectionUVE(trs, direction);
    Vector3UVE back{};

    ASSERT_TRUE(TryInverseTransformDirectionUVE(trs, moved, back));
    EXPECT_NEAR(back.x, direction.x, kEpsilon);
    EXPECT_NEAR(back.y, direction.y, kEpsilon);
    EXPECT_NEAR(back.z, direction.z, kEpsilon);
}

TEST(TrsUVETest, InverseTransformDirection_MatchesLegacyUnrotateThenDivide) {
    // Pins the gizmo drag path's long-standing world-to-local arithmetic: unrotate first, then
    // divide component-wise. Rz90 parent at uniform scale 2, world delta (2,0,0):
    // unrotated = Rz-90*(2,0,0) = (0,-2,0); local = (0,-2,0)/2 = (0,-1,0).
    const TrsUVE parent{Vector3UVE{9.0F, 9.0F, 9.0F},
                        QuaternionUVE{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                      std::numbers::sqrt2_v<float> / 2.0F},
                        Vector3UVE{2.0F, 2.0F, 2.0F}};
    Vector3UVE local{};

    ASSERT_TRUE(TryInverseTransformDirectionUVE(parent, Vector3UVE{2.0F, 0.0F, 0.0F}, local));
    EXPECT_NEAR(local.x, 0.0F, kEpsilon);
    EXPECT_NEAR(local.y, -1.0F, kEpsilon);
    EXPECT_NEAR(local.z, 0.0F, kEpsilon);
}

TEST(TrsUVETest, TryInverseTransform_NonFiniteInput_ReturnsFalse) {
    const TrsUVE finite = TrsUVE::IdentityUVE();
    const TrsUVE nonFinite{Vector3UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
                           QuaternionUVE{}, Vector3UVE{1.0F, 1.0F, 1.0F}};
    Vector3UVE out{};

    EXPECT_FALSE(TryInverseTransformPointUVE(
        nonFinite, Vector3UVE{1.0F, 2.0F, 3.0F}, out));
    EXPECT_FALSE(TryInverseTransformPointUVE(
        finite, Vector3UVE{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, out));
    EXPECT_FALSE(TryInverseTransformDirectionUVE(
        finite, Vector3UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, out));
}

TEST(TrsUVETest, TryInverseTransform_DegenerateRotation_ReturnsFalse) {
    const TrsUVE zeroRotation{Vector3UVE{}, QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F},
                              Vector3UVE{1.0F, 1.0F, 1.0F}};
    Vector3UVE out{};

    EXPECT_FALSE(TryInverseTransformPointUVE(zeroRotation, Vector3UVE{1.0F, 2.0F, 3.0F}, out));
    EXPECT_FALSE(TryInverseTransformDirectionUVE(zeroRotation, Vector3UVE{1.0F, 0.0F, 0.0F}, out));
}

TEST(TrsUVETest, TryInverseTransform_ZeroScale_ReturnsFalse) {
    const TrsUVE zeroScale{Vector3UVE{}, QuaternionUVE{}, Vector3UVE{1.0F, 0.0F, 1.0F}};
    Vector3UVE out{};

    EXPECT_FALSE(TryInverseTransformPointUVE(zeroScale, Vector3UVE{1.0F, 2.0F, 3.0F}, out));
    EXPECT_FALSE(TryInverseTransformDirectionUVE(zeroScale, Vector3UVE{1.0F, 0.0F, 0.0F}, out));
}

TEST(TrsUVETest, ToStringUVE_ContainsAllFields) {
    const TrsUVE trs{Vector3UVE{1.0F, 2.0F, 3.0F}, QuaternionUVE{}, Vector3UVE{4.0F, 5.0F, 6.0F}};
    const std::string text = ToStringUVE(trs);

    EXPECT_NE(text.find("Trs("), std::string::npos);
    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("6.000000"), std::string::npos);
}

TEST(TrsUVETest, InverseTransformRotation_IdentityParent_ReturnsDeltaUnchanged) {
    QuaternionUVE worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{1.0F, 0.0F, 0.0F}, 0.5F, worldDelta));
    QuaternionUVE local{};

    ASSERT_TRUE(TryInverseTransformRotationUVE(TrsUVE::IdentityUVE(), worldDelta, local));
    EXPECT_FLOAT_EQ(local.x, worldDelta.x);
    EXPECT_FLOAT_EQ(local.y, worldDelta.y);
    EXPECT_FLOAT_EQ(local.z, worldDelta.z);
    EXPECT_FLOAT_EQ(local.w, worldDelta.w);
}

TEST(TrsUVETest, InverseTransformRotation_ConjugatesWorldDeltaIntoParentFrame) {
    // Parent yawed +90 degrees about Y; world delta is +90 degrees about world X. Conjugating by
    // the parent yaw moves the X-axis delta onto the parent's +Z axis: local == Rz(+90 degrees).
    // (Yaw +90 maps parent-frame +Z onto world-frame +X, so a world X spin is a local Z spin.)
    QuaternionUVE parentRotation{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(
        Vector3UVE{0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float> / 2.0F, parentRotation));
    QuaternionUVE worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(
        Vector3UVE{1.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> / 2.0F, worldDelta));
    QuaternionUVE expectedLocal{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(
        Vector3UVE{0.0F, 0.0F, 1.0F}, std::numbers::pi_v<float> / 2.0F, expectedLocal));
    const TrsUVE parent{Vector3UVE{}, parentRotation, Vector3UVE{1.0F, 1.0F, 1.0F}};
    QuaternionUVE local{};

    ASSERT_TRUE(TryInverseTransformRotationUVE(parent, worldDelta, local));
    EXPECT_NEAR(local.x, expectedLocal.x, kEpsilon);
    EXPECT_NEAR(local.y, expectedLocal.y, kEpsilon);
    EXPECT_NEAR(local.z, expectedLocal.z, kEpsilon);
    EXPECT_NEAR(local.w, expectedLocal.w, kEpsilon);

    // The local delta must reproduce the world delta back under the parent: the composed world
    // rotation equals the delta applied to the old world rotation.
    const QuaternionUVE recomposed = MultiplyUVE(parentRotation, local);
    const QuaternionUVE expectedWorld = MultiplyUVE(worldDelta, parentRotation);
    EXPECT_NEAR(recomposed.x, expectedWorld.x, kEpsilon);
    EXPECT_NEAR(recomposed.y, expectedWorld.y, kEpsilon);
    EXPECT_NEAR(recomposed.z, expectedWorld.z, kEpsilon);
    EXPECT_NEAR(recomposed.w, expectedWorld.w, kEpsilon);
}

TEST(TrsUVETest, InverseTransformRotation_IgnoresTranslationAndScale) {
    QuaternionUVE parentRotation{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{0.0F, 1.0F, 0.0F}, 0.7F, parentRotation));
    QuaternionUVE worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{1.0F, 0.0F, 0.0F}, 0.3F, worldDelta));
    const TrsUVE plain{Vector3UVE{}, parentRotation, Vector3UVE{1.0F, 1.0F, 1.0F}};
    const TrsUVE wild{Vector3UVE{9.0F, -4.0F, 2.0F}, parentRotation,
                      Vector3UVE{2.0F, 0.5F, -3.0F}};
    QuaternionUVE localPlain{};
    QuaternionUVE localWild{};

    ASSERT_TRUE(TryInverseTransformRotationUVE(plain, worldDelta, localPlain));
    ASSERT_TRUE(TryInverseTransformRotationUVE(wild, worldDelta, localWild));
    EXPECT_EQ(localPlain, localWild);

    // Non-finite translation/scale are still accepted: the retired sandwich never read them, and
    // neither does this function - a degenerate scale must not break rotate drags.
    const TrsUVE nonFiniteScale{Vector3UVE{}, parentRotation,
                                Vector3UVE{std::numeric_limits<float>::infinity(), 1.0F, 1.0F}};
    QuaternionUVE localNonFinite{};

    ASSERT_TRUE(TryInverseTransformRotationUVE(nonFiniteScale, worldDelta, localNonFinite));
    EXPECT_EQ(localPlain, localNonFinite);
}

TEST(TrsUVETest, InverseTransformRotation_DegenerateOrNonFinite_ReturnsFalse) {
    const TrsUVE zeroRotation{Vector3UVE{}, QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F},
                              Vector3UVE{1.0F, 1.0F, 1.0F}};
    const TrsUVE finite = TrsUVE::IdentityUVE();
    QuaternionUVE worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{1.0F, 0.0F, 0.0F}, 0.3F, worldDelta));
    const QuaternionUVE sentinel{1.0F, 2.0F, 3.0F, 4.0F};
    QuaternionUVE out = sentinel;

    EXPECT_FALSE(TryInverseTransformRotationUVE(zeroRotation, worldDelta, out));
    EXPECT_EQ(out, sentinel);
    const QuaternionUVE nonFiniteDelta{
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F};
    EXPECT_FALSE(TryInverseTransformRotationUVE(finite, nonFiniteDelta, out));
    EXPECT_EQ(out, sentinel);
    const TrsUVE nonFiniteRotation{Vector3UVE{}, nonFiniteDelta, Vector3UVE{1.0F, 1.0F, 1.0F}};
    EXPECT_FALSE(TryInverseTransformRotationUVE(nonFiniteRotation, worldDelta, out));
    EXPECT_EQ(out, sentinel);
}

} // namespace
} // namespace UVE::Math::Tests
