// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/quaternion_uve.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(QuaternionUVETest, DefaultConstruction_IsIdentity) {
    constexpr QuaternionUVE rotation{};
    EXPECT_EQ(rotation.x, 0.0F);
    EXPECT_EQ(rotation.y, 0.0F);
    EXPECT_EQ(rotation.z, 0.0F);
    EXPECT_EQ(rotation.w, 1.0F);
}

TEST(QuaternionUVETest, EqualityOperators_CompareAllFourComponents) {
    constexpr QuaternionUVE a{0.0F, 0.0F, 0.0F, 1.0F};
    constexpr QuaternionUVE b{0.0F, 0.0F, 0.0F, 1.0F};
    constexpr QuaternionUVE c{1.0F, 0.0F, 0.0F, 0.0F};

    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(QuaternionUVETest, MultiplyUVE_WithIdentity_IsUnchanged) {
    constexpr QuaternionUVE identity{};
    constexpr QuaternionUVE rotation{0.1F, 0.2F, 0.3F, 0.9F};

    EXPECT_EQ(MultiplyUVE(rotation, identity), rotation);
    EXPECT_EQ(MultiplyUVE(identity, rotation), rotation);
}

TEST(QuaternionUVETest, MultiplyUVE_TwoNinetyDegreeZRotations_YieldsOneHundredEightyDegreeRotation) {
    const float halfNinety = std::sqrt(2.0F) / 2.0F;
    const QuaternionUVE ninetyAboutZ{0.0F, 0.0F, halfNinety, halfNinety};

    const QuaternionUVE oneEighty = MultiplyUVE(ninetyAboutZ, ninetyAboutZ);

    EXPECT_NEAR(oneEighty.x, 0.0F, kEpsilon);
    EXPECT_NEAR(oneEighty.y, 0.0F, kEpsilon);
    EXPECT_NEAR(oneEighty.z, 1.0F, kEpsilon);
    EXPECT_NEAR(oneEighty.w, 0.0F, kEpsilon);
}

TEST(QuaternionUVETest, RotateVectorUVE_WithIdentity_IsUnchanged) {
    constexpr QuaternionUVE identity{};
    constexpr Vector3UVE vector{1.0F, 2.0F, 3.0F};

    EXPECT_EQ(RotateVectorUVE(identity, vector), vector);
}

TEST(QuaternionUVETest, RotateVectorUVE_OneHundredEightyDegreesAboutZ_NegatesXAndY) {
    constexpr QuaternionUVE oneEightyAboutZ{0.0F, 0.0F, 1.0F, 0.0F};
    constexpr Vector3UVE vector{1.0F, 1.0F, 5.0F};

    const Vector3UVE rotated = RotateVectorUVE(oneEightyAboutZ, vector);

    EXPECT_NEAR(rotated.x, -1.0F, kEpsilon);
    EXPECT_NEAR(rotated.y, -1.0F, kEpsilon);
    EXPECT_NEAR(rotated.z, 5.0F, kEpsilon);
}

TEST(QuaternionUVETest, RotateVectorUVE_NinetyDegreesAboutZ_RotatesXAxisToYAxis) {
    const float halfNinety = std::sqrt(2.0F) / 2.0F;
    const QuaternionUVE ninetyAboutZ{0.0F, 0.0F, halfNinety, halfNinety};
    constexpr Vector3UVE xAxis{1.0F, 0.0F, 0.0F};

    const Vector3UVE rotated = RotateVectorUVE(ninetyAboutZ, xAxis);

    EXPECT_NEAR(rotated.x, 0.0F, kEpsilon);
    EXPECT_NEAR(rotated.y, 1.0F, kEpsilon);
    EXPECT_NEAR(rotated.z, 0.0F, kEpsilon);
}

TEST(QuaternionUVETest, CheckedHelpers_NormalizeInvertAndConstructAxisAngle) {
    QuaternionUVE normalized{};
    EXPECT_TRUE(TryNormalizeUVE(QuaternionUVE{0.0F, 0.0F, 2.0F, 2.0F}, normalized));
    EXPECT_NEAR(LengthSquaredUVE(normalized), 1.0F, kEpsilon);
    EXPECT_TRUE(IsFiniteUVE(normalized));

    QuaternionUVE inverse{};
    ASSERT_TRUE(TryInverseUVE(normalized, inverse));
    const QuaternionUVE identity = MultiplyUVE(normalized, inverse);
    EXPECT_NEAR(identity.x, 0.0F, kEpsilon);
    EXPECT_NEAR(identity.y, 0.0F, kEpsilon);
    EXPECT_NEAR(identity.z, 0.0F, kEpsilon);
    EXPECT_NEAR(identity.w, 1.0F, kEpsilon);

    QuaternionUVE ninetyAboutZ{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{0.0F, 0.0F, 1.0F}, std::numbers::pi_v<float> * 0.5F,
                                     ninetyAboutZ));
    const Vector3UVE rotated = RotateVectorUVE(ninetyAboutZ, Vector3UVE{1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(rotated.x, 0.0F, kEpsilon);
    EXPECT_NEAR(rotated.y, 1.0F, kEpsilon);
    EXPECT_NEAR(rotated.z, 0.0F, kEpsilon);
}

TEST(QuaternionUVETest, CheckedHelpers_LargeFiniteInputUsesScaledMagnitude) {
    const float maximum = std::numeric_limits<float>::max();
    const QuaternionUVE large{maximum, maximum, 0.0F, 0.0F};

    QuaternionUVE normalized{};
    ASSERT_TRUE(TryNormalizeUVE(large, normalized));
    EXPECT_TRUE(IsFiniteUVE(normalized));
    EXPECT_NEAR(LengthSquaredUVE(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.70710677F, 1.0e-6F);
    EXPECT_NEAR(normalized.y, 0.70710677F, 1.0e-6F);

    QuaternionUVE inverse{};
    ASSERT_TRUE(TryInverseUVE(large, inverse));
    EXPECT_TRUE(IsFiniteUVE(inverse));
    const QuaternionUVE identity = MultiplyUVE(large, inverse);
    EXPECT_NEAR(identity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-6F);
    EXPECT_NEAR(identity.w, 1.0F, 1.0e-6F);
}

TEST(QuaternionUVETest, CheckedHelpers_AxisAngleAcceptsLargeFiniteAxis) {
    const float maximum = std::numeric_limits<float>::max();
    QuaternionUVE rotation{};
    ASSERT_TRUE(TryMakeAxisAngleUVE(Vector3UVE{maximum, maximum, 0.0F}, std::numbers::pi_v<float> * 0.5F,
                                    rotation));
    EXPECT_TRUE(IsFiniteUVE(rotation));
    EXPECT_NEAR(LengthSquaredUVE(rotation), 1.0F, kEpsilon);
}

TEST(QuaternionUVETest, CheckedHelpers_LookAtAcceptsLargeFiniteDirectionAndUp) {
    const float maximum = std::numeric_limits<float>::max();
    QuaternionUVE lookAt{};
    ASSERT_TRUE(TryMakeLookAtUVE(Vector3UVE{maximum, 0.0F, 0.0F},
                                 Vector3UVE{0.0F, maximum, 0.0F}, lookAt));
    EXPECT_TRUE(IsFiniteUVE(lookAt));
    EXPECT_NEAR(LengthSquaredUVE(lookAt), 1.0F, kEpsilon);
}

TEST(QuaternionUVETest, CheckedHelpers_RejectNonFiniteOrZeroInputWithoutChangingOutput) {
    QuaternionUVE preserved{1.0F, 2.0F, 3.0F, 4.0F};
    EXPECT_FALSE(TryNormalizeUVE(QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F}, preserved));
    EXPECT_EQ(preserved, (QuaternionUVE{1.0F, 2.0F, 3.0F, 4.0F}));
    EXPECT_FALSE(TryInverseUVE(QuaternionUVE{NAN, 0.0F, 0.0F, 1.0F}, preserved));
    EXPECT_EQ(preserved, (QuaternionUVE{1.0F, 2.0F, 3.0F, 4.0F}));
    EXPECT_FALSE(TryMakeAxisAngleUVE(Vector3UVE{}, 0.0F, preserved));
    EXPECT_EQ(preserved, (QuaternionUVE{1.0F, 2.0F, 3.0F, 4.0F}));
}

TEST(QuaternionUVETest, CheckedHelpers_EulerLookAtSlerpAndAxisAngleDecomposition) {
    QuaternionUVE euler{};
    ASSERT_TRUE(TryMakeEulerUVE(Vector3UVE{0.0F, 0.0F, std::numbers::pi_v<float> * 0.5F}, euler));
    Vector3UVE axis{};
    float radians = 0.0F;
    ASSERT_TRUE(TryToAxisAngleUVE(euler, axis, radians));
    EXPECT_NEAR(axis.z, 1.0F, kEpsilon);
    EXPECT_NEAR(radians, std::numbers::pi_v<float> * 0.5F, kEpsilon);

    QuaternionUVE lookAt{};
    ASSERT_TRUE(TryMakeLookAtUVE(Vector3UVE{0.0F, 0.0F, 1.0F}, Vector3UVE{0.0F, 1.0F, 0.0F}, lookAt));
    const Vector3UVE forward = RotateVectorUVE(lookAt, Vector3UVE{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(forward.x, 0.0F, kEpsilon);
    EXPECT_NEAR(forward.y, 0.0F, kEpsilon);
    EXPECT_NEAR(forward.z, 1.0F, kEpsilon);

    QuaternionUVE half{};
    ASSERT_TRUE(TrySlerpUVE(QuaternionUVE{}, euler, 0.5F, half));
    EXPECT_NEAR(LengthSquaredUVE(half), 1.0F, kEpsilon);
    EXPECT_FALSE(TryMakeLookAtUVE(Vector3UVE{0.0F, 0.0F, 0.0F}, Vector3UVE{0.0F, 1.0F, 0.0F}, half));
}

TEST(QuaternionUVETest, TryToEulerUVE_RoundTripsWithTryMakeEulerUVE) {
    const std::array<Vector3UVE, 5> anglesRadians{
        Vector3UVE{0.0F, 0.0F, 0.0F},
        Vector3UVE{0.3F, 0.0F, 0.0F},
        Vector3UVE{0.0F, -0.6F, 0.0F},
        Vector3UVE{0.0F, 0.0F, 1.1F},
        Vector3UVE{0.4F, -0.5F, 0.7F},
    };
    for (const Vector3UVE& original : anglesRadians) {
        QuaternionUVE rotation{};
        ASSERT_TRUE(TryMakeEulerUVE(original, rotation));
        Vector3UVE recovered{};
        ASSERT_TRUE(TryToEulerUVE(rotation, recovered));
        EXPECT_NEAR(recovered.x, original.x, kEpsilon);
        EXPECT_NEAR(recovered.y, original.y, kEpsilon);
        EXPECT_NEAR(recovered.z, original.z, kEpsilon);
    }
}

TEST(QuaternionUVETest, TryToEulerUVE_RejectsNonFiniteInput) {
    Vector3UVE outRadians{};
    const QuaternionUVE nonFinite{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F};
    EXPECT_FALSE(TryToEulerUVE(nonFinite, outRadians));
}

TEST(QuaternionUVETest, RotateVectorUVE_PreservesFiniteExtremeHalfTurn) {
    const float maximum = std::numeric_limits<float>::max();
    const Vector3UVE result = RotateVectorUVE(QuaternionUVE{0.0F, 0.0F, 1.0F, 0.0F},
                                               Vector3UVE{maximum, -maximum, 0.0F});

    EXPECT_TRUE(std::isfinite(result.x));
    EXPECT_TRUE(std::isfinite(result.y));
    EXPECT_TRUE(std::isfinite(result.z));
    EXPECT_FLOAT_EQ(result.x, -maximum);
    EXPECT_FLOAT_EQ(result.y, maximum);
    EXPECT_FLOAT_EQ(result.z, 0.0F);
}

TEST(QuaternionUVETest, ToStringUVE_FormatsAllFourComponents) {
    const QuaternionUVE rotation{0.0F, 0.0F, 0.0F, 1.0F};
    const std::string text = ToStringUVE(rotation);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
}

/// Sign-agnostic rotation comparison: q and -q are the same rotation, so comparing components
/// directly reports differences that do not exist. |dot| == 1 is the honest test.
[[nodiscard]] bool RepresentSameRotationUVE(const QuaternionUVE& lhs, const QuaternionUVE& rhs,
                                            const float tolerance = 1e-4F) {
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::abs(std::abs(dot) - 1.0F) <= tolerance;
}

constexpr std::array<EulerOrderUVE, 6U> kAllEulerOrdersUVE{
    EulerOrderUVE::XYZ, EulerOrderUVE::YXZ, EulerOrderUVE::ZYX,
    EulerOrderUVE::XZY, EulerOrderUVE::YZX, EulerOrderUVE::ZXY};

TEST(QuaternionEulerOrderUVETest, XYZIsBitForBitTheExistingTwoArgumentForm) {
    // The compatibility guarantee the whole feature rests on. TryMakeEulerUVE has produced every
    // authored rotation in every saved scene; if the ordered form disagreed with it for XYZ, this
    // change would silently rotate existing content.
    const std::array<Vector3UVE, 6U> samples{
        Vector3UVE{0.1F, 0.2F, 0.3F},   Vector3UVE{-0.8F, 1.0F, 0.26F},
        Vector3UVE{1.4F, -0.5F, 2.1F},  Vector3UVE{0.0F, 0.0F, 0.0F},
        Vector3UVE{-2.9F, 0.35F, -1.6F}, Vector3UVE{1.5708F, 0.7854F, 0.5236F}};

    for (const Vector3UVE& radians : samples) {
        QuaternionUVE viaExisting{};
        QuaternionUVE viaOrdered{};
        ASSERT_TRUE(TryMakeEulerUVE(radians, viaExisting));
        ASSERT_TRUE(TryMakeEulerOrderedUVE(radians, EulerOrderUVE::XYZ, viaOrdered));
        EXPECT_FLOAT_EQ(viaExisting.x, viaOrdered.x);
        EXPECT_FLOAT_EQ(viaExisting.y, viaOrdered.y);
        EXPECT_FLOAT_EQ(viaExisting.z, viaOrdered.z);
        EXPECT_FLOAT_EQ(viaExisting.w, viaOrdered.w);
    }
}

TEST(QuaternionEulerOrderUVETest, EveryOrderRoundTrips) {
    const std::array<Vector3UVE, 5U> samples{
        Vector3UVE{0.17F, 0.35F, 0.52F},  Vector3UVE{-0.79F, 1.05F, 0.26F},
        Vector3UVE{1.40F, -0.52F, 2.09F}, Vector3UVE{0.09F, 0.09F, 0.09F},
        Vector3UVE{-2.97F, 0.35F, -1.66F}};

    for (const EulerOrderUVE order : kAllEulerOrdersUVE) {
        for (const Vector3UVE& radians : samples) {
            QuaternionUVE built{};
            ASSERT_TRUE(TryMakeEulerOrderedUVE(radians, order, built))
                << "order " << static_cast<int>(order);
            Vector3UVE extracted{};
            ASSERT_TRUE(TryToEulerOrderedUVE(built, order, extracted));
            QuaternionUVE rebuilt{};
            ASSERT_TRUE(TryMakeEulerOrderedUVE(extracted, order, rebuilt));
            // The ROTATION must round-trip. The angles need not be the same triple - away from a
            // singularity they will be, but asserting the rotation is what actually matters and
            // is what stays true at the poles.
            EXPECT_TRUE(RepresentSameRotationUVE(built, rebuilt))
                << "order " << static_cast<int>(order);
        }
    }
}

TEST(QuaternionEulerOrderUVETest, EveryOrderSurvivesItsOwnGimbalLockPose) {
    // Each order has a different singular pose - that is the entire reason six exist. At the pole
    // the angle split is ambiguous, but the rotation must still rebuild exactly, or switching
    // order would corrupt an object rather than just relabel its angles.
    const std::array<Vector3UVE, 6U> singularPoses{
        Vector3UVE{0.0F, 1.5707963F, 0.0F},  // XYZ: middle axis is Y
        Vector3UVE{1.5707963F, 0.0F, 0.0F},  // YXZ: middle axis is X
        Vector3UVE{0.0F, 1.5707963F, 0.0F},  // ZYX
        Vector3UVE{0.0F, 0.0F, 1.5707963F},  // XZY
        Vector3UVE{0.0F, 0.0F, 1.5707963F},  // YZX
        Vector3UVE{1.5707963F, 0.0F, 0.0F}}; // ZXY

    for (std::size_t index = 0U; index < kAllEulerOrdersUVE.size(); ++index) {
        const EulerOrderUVE order = kAllEulerOrdersUVE[index];
        QuaternionUVE built{};
        ASSERT_TRUE(TryMakeEulerOrderedUVE(singularPoses[index], order, built));
        Vector3UVE extracted{};
        ASSERT_TRUE(TryToEulerOrderedUVE(built, order, extracted));
        QuaternionUVE rebuilt{};
        ASSERT_TRUE(TryMakeEulerOrderedUVE(extracted, order, rebuilt));
        EXPECT_TRUE(RepresentSameRotationUVE(built, rebuilt))
            << "order " << static_cast<int>(order) << " lost the rotation at its singular pose";
    }
}

TEST(QuaternionEulerOrderUVETest, OrdersProduceDifferentRotationsFromTheSameAngles) {
    // If two orders agreed on a general pose, one of them would be misimplemented and the
    // dropdown would be decorative. Axis-aligned angles are excluded deliberately: those SHOULD
    // agree, and using them here would make the test pass for the wrong reason.
    const Vector3UVE radians{0.6F, 0.9F, 1.2F};
    std::array<QuaternionUVE, 6U> built{};
    for (std::size_t index = 0U; index < kAllEulerOrdersUVE.size(); ++index) {
        ASSERT_TRUE(TryMakeEulerOrderedUVE(radians, kAllEulerOrdersUVE[index], built[index]));
    }
    for (std::size_t lhs = 0U; lhs < built.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1U; rhs < built.size(); ++rhs) {
            EXPECT_FALSE(RepresentSameRotationUVE(built[lhs], built[rhs]))
                << "orders " << lhs << " and " << rhs << " are indistinguishable";
        }
    }
}

TEST(QuaternionEulerOrderUVETest, NonFiniteInputIsRejectedForEveryOrder) {
    const Vector3UVE notFinite{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const QuaternionUVE zeroLength{0.0F, 0.0F, 0.0F, 0.0F};
    for (const EulerOrderUVE order : kAllEulerOrdersUVE) {
        QuaternionUVE rotation{};
        EXPECT_FALSE(TryMakeEulerOrderedUVE(notFinite, order, rotation));
        Vector3UVE radians{};
        EXPECT_FALSE(TryToEulerOrderedUVE(zeroLength, order, radians))
            << "a zero-length quaternion has no rotation to extract";
    }
}

TEST(QuaternionEulerOrderUVETest, AnUnknownOrderValueIsRejectedRatherThanSilentlyTreatedAsXYZ) {
    // The enum crosses a file format and an Inspector dropdown, so a value outside the six can
    // arrive from a hand-edited scene. Falling through to XYZ would silently rotate the object;
    // refusing leaves the caller's output untouched and the failure visible.
    const auto bogus = static_cast<EulerOrderUVE>(200);
    QuaternionUVE rotation{1.0F, 0.0F, 0.0F, 0.0F};
    const QuaternionUVE before = rotation;
    EXPECT_FALSE(TryMakeEulerOrderedUVE(Vector3UVE{0.1F, 0.2F, 0.3F}, bogus, rotation));
    EXPECT_FLOAT_EQ(rotation.x, before.x) << "a rejected call must not write its output";

    Vector3UVE radians{7.0F, 7.0F, 7.0F};
    const Vector3UVE radiansBefore = radians;
    EXPECT_FALSE(TryToEulerOrderedUVE(QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F}, bogus, radians));
    EXPECT_FLOAT_EQ(radians.x, radiansBefore.x);
}

} // namespace
} // namespace UVE::Math::Tests
