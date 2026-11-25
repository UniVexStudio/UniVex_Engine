// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "trs.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <string>

#include "quaternion.h"
#include "vector3.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-4F;

TEST(TrsTest, DefaultConstruction_IsIdentity) {
    constexpr Trs trs{};
    EXPECT_EQ(trs.translation, (Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(trs.rotation, (Quaternion{0.0F, 0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(trs.scale, (Vector3{1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(trs, Trs::Identity());
}

TEST(TrsTest, EqualityOperators_CompareAllFields) {
    const Trs a{Vector3{1.0F, 2.0F, 3.0F}, Quaternion{}, Vector3{1.0F, 1.0F, 1.0F}};
    const Trs b = a;
    const Trs moved{Vector3{9.0F, 2.0F, 3.0F}, Quaternion{}, Vector3{1.0F, 1.0F, 1.0F}};
    const Trs turned{
        Vector3{1.0F, 2.0F, 3.0F}, Quaternion{0.0F, 0.0F, 1.0F, 0.0F}, Vector3{1.0F, 1.0F, 1.0F}};
    const Trs scaled{Vector3{1.0F, 2.0F, 3.0F}, Quaternion{}, Vector3{2.0F, 1.0F, 1.0F}};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != moved);
    EXPECT_TRUE(a != turned);
    EXPECT_TRUE(a != scaled);
}

TEST(TrsTest, Compose_WithIdentity_IsUnchanged) {
    const Trs trs{Vector3{1.0F, 2.0F, 3.0F},
                     Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3{2.0F, 3.0F, 4.0F}};

    EXPECT_EQ(Compose(Trs::Identity(), trs), trs);
    EXPECT_EQ(Compose(trs, Trs::Identity()), trs);
}

TEST(TrsTest, Compose_KnownTransforms_MatchesHandComputedValue) {
    // Parent: translated (1,0,0), rotated +90 degrees about Z, uniformly scaled by 2.
    const Trs parent{Vector3{1.0F, 0.0F, 0.0F},
                        Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                      std::numbers::sqrt2_v<float> / 2.0F},
                        Vector3{2.0F, 2.0F, 2.0F}};
    // Child: translated (1,0,0), unrotated, uniformly scaled by 3.
    const Trs child{Vector3{1.0F, 0.0F, 0.0F}, Quaternion{}, Vector3{3.0F, 3.0F, 3.0F}};

    // scale = (6,6,6); rotation = parent's 90-degree Z; translation = (1,0,0) + Rz90*(2,0,0) = (1,2,0).
    const Trs composed = Compose(parent, child);

    EXPECT_EQ(composed.scale, (Vector3{6.0F, 6.0F, 6.0F}));
    EXPECT_NEAR(composed.rotation.x, 0.0F, kEpsilon);
    EXPECT_NEAR(composed.rotation.y, 0.0F, kEpsilon);
    EXPECT_NEAR(composed.rotation.z, std::numbers::sqrt2_v<float> / 2.0F, kEpsilon);
    EXPECT_NEAR(composed.rotation.w, std::numbers::sqrt2_v<float> / 2.0F, kEpsilon);
    EXPECT_NEAR(composed.translation.x, 1.0F, kEpsilon);
    EXPECT_NEAR(composed.translation.y, 2.0F, kEpsilon);
    EXPECT_NEAR(composed.translation.z, 0.0F, kEpsilon);
}

TEST(TrsTest, Compose_IsAssociative) {
    // Uniform scales: the domain associativity holds over (a uniform scale commutes past any
    // rotation, so grouping a chain cannot matter). Non-uniform scales under rotation are
    // order-sensitive by construction - see Compose's doc comment.
    const Trs a{Vector3{1.0F, 2.0F, 3.0F},
                   Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                 std::numbers::sqrt2_v<float> / 2.0F},
                   Vector3{2.0F, 2.0F, 2.0F}};
    const Trs b{Vector3{-1.0F, 0.5F, 2.0F},
                   Quaternion{std::numbers::sqrt2_v<float> / 2.0F, 0.0F, 0.0F,
                                 std::numbers::sqrt2_v<float> / 2.0F},
                   Vector3{3.0F, 3.0F, 3.0F}};
    const Trs c{Vector3{0.0F, 0.0F, 1.0F}, Quaternion{}, Vector3{0.5F, 0.5F, 0.5F}};

    const Trs leftFirst = Compose(Compose(a, b), c);
    const Trs rightFirst = Compose(a, Compose(b, c));

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

TEST(TrsTest, TransformPoint_KnownTransform_MatchesHandComputedValue) {
    const Trs trs{Vector3{1.0F, 0.0F, 0.0F},
                     Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3{2.0F, 2.0F, 2.0F}};

    // (1,0,0) + Rz90*((2,2,2) * (1,0,0)) = (1,0,0) + Rz90*(2,0,0) = (1,2,0).
    const Vector3 result = TransformPoint(trs, Vector3{1.0F, 0.0F, 0.0F});

    EXPECT_NEAR(result.x, 1.0F, kEpsilon);
    EXPECT_NEAR(result.y, 2.0F, kEpsilon);
    EXPECT_NEAR(result.z, 0.0F, kEpsilon);
}

TEST(TrsTest, TransformDirection_IgnoresTranslation) {
    const Trs moved{Vector3{5.0F, 6.0F, 7.0F}, Quaternion{}, Vector3{2.0F, 3.0F, 4.0F}};
    const Trs unmoved{Vector3{}, Quaternion{}, Vector3{2.0F, 3.0F, 4.0F}};

    EXPECT_EQ(TransformDirection(moved, Vector3{1.0F, 1.0F, 1.0F}),
              TransformDirection(unmoved, Vector3{1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(TransformDirection(moved, Vector3{1.0F, 1.0F, 1.0F}),
              (Vector3{2.0F, 3.0F, 4.0F}));
}

TEST(TrsTest, InverseTransformPoint_RoundTripsThroughNonUniformRotatedParent) {
    // The case with NO materialized inverse TRS (rotation and non-uniform scale do not commute):
    // forward-then-inverse must still round-trip, which is why the inverse is offered as
    // application functions rather than as an inverse value.
    const Trs trs{Vector3{3.0F, -4.0F, 5.0F},
                     Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3{2.0F, 3.0F, 4.0F}};
    const Vector3 point{1.0F, -2.0F, 3.0F};

    const Vector3 moved = TransformPoint(trs, point);
    Vector3 back{};

    ASSERT_TRUE(TryInverseTransformPoint(trs, moved, back));
    EXPECT_NEAR(back.x, point.x, kEpsilon);
    EXPECT_NEAR(back.y, point.y, kEpsilon);
    EXPECT_NEAR(back.z, point.z, kEpsilon);
}

TEST(TrsTest, InverseTransformDirection_RoundTrips) {
    const Trs trs{Vector3{3.0F, -4.0F, 5.0F},
                     Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                   std::numbers::sqrt2_v<float> / 2.0F},
                     Vector3{2.0F, 3.0F, 4.0F}};
    const Vector3 direction{1.0F, 1.0F, 1.0F};

    const Vector3 moved = TransformDirection(trs, direction);
    Vector3 back{};

    ASSERT_TRUE(TryInverseTransformDirection(trs, moved, back));
    EXPECT_NEAR(back.x, direction.x, kEpsilon);
    EXPECT_NEAR(back.y, direction.y, kEpsilon);
    EXPECT_NEAR(back.z, direction.z, kEpsilon);
}

TEST(TrsTest, InverseTransformDirection_MatchesLegacyUnrotateThenDivide) {
    // Pins the gizmo drag path's long-standing world-to-local arithmetic: unrotate first, then
    // divide component-wise. Rz90 parent at uniform scale 2, world delta (2,0,0):
    // unrotated = Rz-90*(2,0,0) = (0,-2,0); local = (0,-2,0)/2 = (0,-1,0).
    const Trs parent{Vector3{9.0F, 9.0F, 9.0F},
                        Quaternion{0.0F, 0.0F, std::numbers::sqrt2_v<float> / 2.0F,
                                      std::numbers::sqrt2_v<float> / 2.0F},
                        Vector3{2.0F, 2.0F, 2.0F}};
    Vector3 local{};

    ASSERT_TRUE(TryInverseTransformDirection(parent, Vector3{2.0F, 0.0F, 0.0F}, local));
    EXPECT_NEAR(local.x, 0.0F, kEpsilon);
    EXPECT_NEAR(local.y, -1.0F, kEpsilon);
    EXPECT_NEAR(local.z, 0.0F, kEpsilon);
}

TEST(TrsTest, TryInverseTransform_NonFiniteInput_ReturnsFalse) {
    const Trs finite = Trs::Identity();
    const Trs nonFinite{Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
                           Quaternion{}, Vector3{1.0F, 1.0F, 1.0F}};
    Vector3 out{};

    EXPECT_FALSE(TryInverseTransformPoint(
        nonFinite, Vector3{1.0F, 2.0F, 3.0F}, out));
    EXPECT_FALSE(TryInverseTransformPoint(
        finite, Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, out));
    EXPECT_FALSE(TryInverseTransformDirection(
        finite, Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F}, out));
}

TEST(TrsTest, TryInverseTransform_DegenerateRotation_ReturnsFalse) {
    const Trs zeroRotation{Vector3{}, Quaternion{0.0F, 0.0F, 0.0F, 0.0F},
                              Vector3{1.0F, 1.0F, 1.0F}};
    Vector3 out{};

    EXPECT_FALSE(TryInverseTransformPoint(zeroRotation, Vector3{1.0F, 2.0F, 3.0F}, out));
    EXPECT_FALSE(TryInverseTransformDirection(zeroRotation, Vector3{1.0F, 0.0F, 0.0F}, out));
}

TEST(TrsTest, TryInverseTransform_ZeroScale_ReturnsFalse) {
    const Trs zeroScale{Vector3{}, Quaternion{}, Vector3{1.0F, 0.0F, 1.0F}};
    Vector3 out{};

    EXPECT_FALSE(TryInverseTransformPoint(zeroScale, Vector3{1.0F, 2.0F, 3.0F}, out));
    EXPECT_FALSE(TryInverseTransformDirection(zeroScale, Vector3{1.0F, 0.0F, 0.0F}, out));
}

TEST(TrsTest, ToString_ContainsAllFields) {
    const Trs trs{Vector3{1.0F, 2.0F, 3.0F}, Quaternion{}, Vector3{4.0F, 5.0F, 6.0F}};
    const std::string text = ToString(trs);

    EXPECT_NE(text.find("Trs("), std::string::npos);
    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("6.000000"), std::string::npos);
}

TEST(TrsTest, InverseTransformRotation_IdentityParent_ReturnsDeltaUnchanged) {
    Quaternion worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{1.0F, 0.0F, 0.0F}, 0.5F, worldDelta));
    Quaternion local{};

    ASSERT_TRUE(TryInverseTransformRotation(Trs::Identity(), worldDelta, local));
    EXPECT_FLOAT_EQ(local.x, worldDelta.x);
    EXPECT_FLOAT_EQ(local.y, worldDelta.y);
    EXPECT_FLOAT_EQ(local.z, worldDelta.z);
    EXPECT_FLOAT_EQ(local.w, worldDelta.w);
}

TEST(TrsTest, InverseTransformRotation_ConjugatesWorldDeltaIntoParentFrame) {
    // Parent yawed +90 degrees about Y; world delta is +90 degrees about world X. Conjugating by
    // the parent yaw moves the X-axis delta onto the parent's +Z axis: local == Rz(+90 degrees).
    // (Yaw +90 maps parent-frame +Z onto world-frame +X, so a world X spin is a local Z spin.)
    Quaternion parentRotation{};
    ASSERT_TRUE(TryMakeAxisAngle(
        Vector3{0.0F, 1.0F, 0.0F}, std::numbers::pi_v<float> / 2.0F, parentRotation));
    Quaternion worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngle(
        Vector3{1.0F, 0.0F, 0.0F}, std::numbers::pi_v<float> / 2.0F, worldDelta));
    Quaternion expectedLocal{};
    ASSERT_TRUE(TryMakeAxisAngle(
        Vector3{0.0F, 0.0F, 1.0F}, std::numbers::pi_v<float> / 2.0F, expectedLocal));
    const Trs parent{Vector3{}, parentRotation, Vector3{1.0F, 1.0F, 1.0F}};
    Quaternion local{};

    ASSERT_TRUE(TryInverseTransformRotation(parent, worldDelta, local));
    EXPECT_NEAR(local.x, expectedLocal.x, kEpsilon);
    EXPECT_NEAR(local.y, expectedLocal.y, kEpsilon);
    EXPECT_NEAR(local.z, expectedLocal.z, kEpsilon);
    EXPECT_NEAR(local.w, expectedLocal.w, kEpsilon);

    // The local delta must reproduce the world delta back under the parent: the composed world
    // rotation equals the delta applied to the old world rotation.
    const Quaternion recomposed = Multiply(parentRotation, local);
    const Quaternion expectedWorld = Multiply(worldDelta, parentRotation);
    EXPECT_NEAR(recomposed.x, expectedWorld.x, kEpsilon);
    EXPECT_NEAR(recomposed.y, expectedWorld.y, kEpsilon);
    EXPECT_NEAR(recomposed.z, expectedWorld.z, kEpsilon);
    EXPECT_NEAR(recomposed.w, expectedWorld.w, kEpsilon);
}

TEST(TrsTest, InverseTransformRotation_IgnoresTranslationAndScale) {
    Quaternion parentRotation{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{0.0F, 1.0F, 0.0F}, 0.7F, parentRotation));
    Quaternion worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{1.0F, 0.0F, 0.0F}, 0.3F, worldDelta));
    const Trs plain{Vector3{}, parentRotation, Vector3{1.0F, 1.0F, 1.0F}};
    const Trs wild{Vector3{9.0F, -4.0F, 2.0F}, parentRotation,
                      Vector3{2.0F, 0.5F, -3.0F}};
    Quaternion localPlain{};
    Quaternion localWild{};

    ASSERT_TRUE(TryInverseTransformRotation(plain, worldDelta, localPlain));
    ASSERT_TRUE(TryInverseTransformRotation(wild, worldDelta, localWild));
    EXPECT_EQ(localPlain, localWild);

    // Non-finite translation/scale are still accepted: the retired sandwich never read them, and
    // neither does this function - a degenerate scale must not break rotate drags.
    const Trs nonFiniteScale{Vector3{}, parentRotation,
                                Vector3{std::numeric_limits<float>::infinity(), 1.0F, 1.0F}};
    Quaternion localNonFinite{};

    ASSERT_TRUE(TryInverseTransformRotation(nonFiniteScale, worldDelta, localNonFinite));
    EXPECT_EQ(localPlain, localNonFinite);
}

TEST(TrsTest, InverseTransformRotation_DegenerateOrNonFinite_ReturnsFalse) {
    const Trs zeroRotation{Vector3{}, Quaternion{0.0F, 0.0F, 0.0F, 0.0F},
                              Vector3{1.0F, 1.0F, 1.0F}};
    const Trs finite = Trs::Identity();
    Quaternion worldDelta{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{1.0F, 0.0F, 0.0F}, 0.3F, worldDelta));
    const Quaternion sentinel{1.0F, 2.0F, 3.0F, 4.0F};
    Quaternion out = sentinel;

    EXPECT_FALSE(TryInverseTransformRotation(zeroRotation, worldDelta, out));
    EXPECT_EQ(out, sentinel);
    const Quaternion nonFiniteDelta{
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F};
    EXPECT_FALSE(TryInverseTransformRotation(finite, nonFiniteDelta, out));
    EXPECT_EQ(out, sentinel);
    const Trs nonFiniteRotation{Vector3{}, nonFiniteDelta, Vector3{1.0F, 1.0F, 1.0F}};
    EXPECT_FALSE(TryInverseTransformRotation(nonFiniteRotation, worldDelta, out));
    EXPECT_EQ(out, sentinel);
}

} // namespace
} // namespace UVE::Math::Tests
