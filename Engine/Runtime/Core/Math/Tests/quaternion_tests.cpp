// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "quaternion.h"

#include <array>
#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(QuaternionTest, DefaultConstruction_IsIdentity) {
    constexpr Quaternion rotation{};
    EXPECT_EQ(rotation.x, 0.0F);
    EXPECT_EQ(rotation.y, 0.0F);
    EXPECT_EQ(rotation.z, 0.0F);
    EXPECT_EQ(rotation.w, 1.0F);
}

TEST(QuaternionTest, EqualityOperators_CompareAllFourComponents) {
    constexpr Quaternion a{0.0F, 0.0F, 0.0F, 1.0F};
    constexpr Quaternion b{0.0F, 0.0F, 0.0F, 1.0F};
    constexpr Quaternion c{1.0F, 0.0F, 0.0F, 0.0F};

    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(QuaternionTest, Multiply_WithIdentity_IsUnchanged) {
    constexpr Quaternion identity{};
    constexpr Quaternion rotation{0.1F, 0.2F, 0.3F, 0.9F};

    EXPECT_EQ(Multiply(rotation, identity), rotation);
    EXPECT_EQ(Multiply(identity, rotation), rotation);
}

TEST(QuaternionTest, Multiply_TwoNinetyDegreeZRotations_YieldsOneHundredEightyDegreeRotation) {
    const float halfNinety = std::sqrt(2.0F) / 2.0F;
    const Quaternion ninetyAboutZ{0.0F, 0.0F, halfNinety, halfNinety};

    const Quaternion oneEighty = Multiply(ninetyAboutZ, ninetyAboutZ);

    EXPECT_NEAR(oneEighty.x, 0.0F, kEpsilon);
    EXPECT_NEAR(oneEighty.y, 0.0F, kEpsilon);
    EXPECT_NEAR(oneEighty.z, 1.0F, kEpsilon);
    EXPECT_NEAR(oneEighty.w, 0.0F, kEpsilon);
}

TEST(QuaternionTest, RotateVector_WithIdentity_IsUnchanged) {
    constexpr Quaternion identity{};
    constexpr Vector3 vector{1.0F, 2.0F, 3.0F};

    EXPECT_EQ(RotateVector(identity, vector), vector);
}

TEST(QuaternionTest, RotateVector_OneHundredEightyDegreesAboutZ_NegatesXAndY) {
    constexpr Quaternion oneEightyAboutZ{0.0F, 0.0F, 1.0F, 0.0F};
    constexpr Vector3 vector{1.0F, 1.0F, 5.0F};

    const Vector3 rotated = RotateVector(oneEightyAboutZ, vector);

    EXPECT_NEAR(rotated.x, -1.0F, kEpsilon);
    EXPECT_NEAR(rotated.y, -1.0F, kEpsilon);
    EXPECT_NEAR(rotated.z, 5.0F, kEpsilon);
}

TEST(QuaternionTest, RotateVector_NinetyDegreesAboutZ_RotatesXAxisToYAxis) {
    const float halfNinety = std::sqrt(2.0F) / 2.0F;
    const Quaternion ninetyAboutZ{0.0F, 0.0F, halfNinety, halfNinety};
    constexpr Vector3 xAxis{1.0F, 0.0F, 0.0F};

    const Vector3 rotated = RotateVector(ninetyAboutZ, xAxis);

    EXPECT_NEAR(rotated.x, 0.0F, kEpsilon);
    EXPECT_NEAR(rotated.y, 1.0F, kEpsilon);
    EXPECT_NEAR(rotated.z, 0.0F, kEpsilon);
}

TEST(QuaternionTest, CheckedHelpers_NormalizeInvertAndConstructAxisAngle) {
    Quaternion normalized{};
    EXPECT_TRUE(TryNormalize(Quaternion{0.0F, 0.0F, 2.0F, 2.0F}, normalized));
    EXPECT_NEAR(LengthSquared(normalized), 1.0F, kEpsilon);
    EXPECT_TRUE(IsFinite(normalized));

    Quaternion inverse{};
    ASSERT_TRUE(TryInverse(normalized, inverse));
    const Quaternion identity = Multiply(normalized, inverse);
    EXPECT_NEAR(identity.x, 0.0F, kEpsilon);
    EXPECT_NEAR(identity.y, 0.0F, kEpsilon);
    EXPECT_NEAR(identity.z, 0.0F, kEpsilon);
    EXPECT_NEAR(identity.w, 1.0F, kEpsilon);

    Quaternion ninetyAboutZ{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{0.0F, 0.0F, 1.0F}, std::numbers::pi_v<float> * 0.5F,
                                     ninetyAboutZ));
    const Vector3 rotated = RotateVector(ninetyAboutZ, Vector3{1.0F, 0.0F, 0.0F});
    EXPECT_NEAR(rotated.x, 0.0F, kEpsilon);
    EXPECT_NEAR(rotated.y, 1.0F, kEpsilon);
    EXPECT_NEAR(rotated.z, 0.0F, kEpsilon);
}

TEST(QuaternionTest, CheckedHelpers_LargeFiniteInputUsesScaledMagnitude) {
    const float maximum = std::numeric_limits<float>::max();
    const Quaternion large{maximum, maximum, 0.0F, 0.0F};

    Quaternion normalized{};
    ASSERT_TRUE(TryNormalize(large, normalized));
    EXPECT_TRUE(IsFinite(normalized));
    EXPECT_NEAR(LengthSquared(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.70710677F, 1.0e-6F);
    EXPECT_NEAR(normalized.y, 0.70710677F, 1.0e-6F);

    Quaternion inverse{};
    ASSERT_TRUE(TryInverse(large, inverse));
    EXPECT_TRUE(IsFinite(inverse));
    const Quaternion identity = Multiply(large, inverse);
    EXPECT_NEAR(identity.x, 0.0F, 1.0e-6F);
    EXPECT_NEAR(identity.y, 0.0F, 1.0e-6F);
    EXPECT_NEAR(identity.z, 0.0F, 1.0e-6F);
    EXPECT_NEAR(identity.w, 1.0F, 1.0e-6F);
}

TEST(QuaternionTest, CheckedHelpers_AxisAngleAcceptsLargeFiniteAxis) {
    const float maximum = std::numeric_limits<float>::max();
    Quaternion rotation{};
    ASSERT_TRUE(TryMakeAxisAngle(Vector3{maximum, maximum, 0.0F}, std::numbers::pi_v<float> * 0.5F,
                                    rotation));
    EXPECT_TRUE(IsFinite(rotation));
    EXPECT_NEAR(LengthSquared(rotation), 1.0F, kEpsilon);
}

TEST(QuaternionTest, CheckedHelpers_LookAtAcceptsLargeFiniteDirectionAndUp) {
    const float maximum = std::numeric_limits<float>::max();
    Quaternion lookAt{};
    ASSERT_TRUE(TryMakeLookAt(Vector3{maximum, 0.0F, 0.0F},
                                 Vector3{0.0F, maximum, 0.0F}, lookAt));
    EXPECT_TRUE(IsFinite(lookAt));
    EXPECT_NEAR(LengthSquared(lookAt), 1.0F, kEpsilon);
}

TEST(QuaternionTest, CheckedHelpers_RejectNonFiniteOrZeroInputWithoutChangingOutput) {
    Quaternion preserved{1.0F, 2.0F, 3.0F, 4.0F};
    EXPECT_FALSE(TryNormalize(Quaternion{0.0F, 0.0F, 0.0F, 0.0F}, preserved));
    EXPECT_EQ(preserved, (Quaternion{1.0F, 2.0F, 3.0F, 4.0F}));
    EXPECT_FALSE(TryInverse(Quaternion{NAN, 0.0F, 0.0F, 1.0F}, preserved));
    EXPECT_EQ(preserved, (Quaternion{1.0F, 2.0F, 3.0F, 4.0F}));
    EXPECT_FALSE(TryMakeAxisAngle(Vector3{}, 0.0F, preserved));
    EXPECT_EQ(preserved, (Quaternion{1.0F, 2.0F, 3.0F, 4.0F}));
}

TEST(QuaternionTest, CheckedHelpers_EulerLookAtSlerpAndAxisAngleDecomposition) {
    Quaternion euler{};
    ASSERT_TRUE(TryMakeEuler(Vector3{0.0F, 0.0F, std::numbers::pi_v<float> * 0.5F}, euler));
    Vector3 axis{};
    float radians = 0.0F;
    ASSERT_TRUE(TryToAxisAngle(euler, axis, radians));
    EXPECT_NEAR(axis.z, 1.0F, kEpsilon);
    EXPECT_NEAR(radians, std::numbers::pi_v<float> * 0.5F, kEpsilon);

    Quaternion lookAt{};
    ASSERT_TRUE(TryMakeLookAt(Vector3{0.0F, 0.0F, 1.0F}, Vector3{0.0F, 1.0F, 0.0F}, lookAt));
    const Vector3 forward = RotateVector(lookAt, Vector3{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(forward.x, 0.0F, kEpsilon);
    EXPECT_NEAR(forward.y, 0.0F, kEpsilon);
    EXPECT_NEAR(forward.z, 1.0F, kEpsilon);

    Quaternion half{};
    ASSERT_TRUE(TrySlerp(Quaternion{}, euler, 0.5F, half));
    EXPECT_NEAR(LengthSquared(half), 1.0F, kEpsilon);
    EXPECT_FALSE(TryMakeLookAt(Vector3{0.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F}, half));
}

TEST(QuaternionTest, TryToEuler_RoundTripsWithTryMakeEuler) {
    const std::array<Vector3, 5> anglesRadians{
        Vector3{0.0F, 0.0F, 0.0F},
        Vector3{0.3F, 0.0F, 0.0F},
        Vector3{0.0F, -0.6F, 0.0F},
        Vector3{0.0F, 0.0F, 1.1F},
        Vector3{0.4F, -0.5F, 0.7F},
    };
    for (const Vector3& original : anglesRadians) {
        Quaternion rotation{};
        ASSERT_TRUE(TryMakeEuler(original, rotation));
        Vector3 recovered{};
        ASSERT_TRUE(TryToEuler(rotation, recovered));
        EXPECT_NEAR(recovered.x, original.x, kEpsilon);
        EXPECT_NEAR(recovered.y, original.y, kEpsilon);
        EXPECT_NEAR(recovered.z, original.z, kEpsilon);
    }
}

TEST(QuaternionTest, TryToEuler_RejectsNonFiniteInput) {
    Vector3 outRadians{};
    const Quaternion nonFinite{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 1.0F};
    EXPECT_FALSE(TryToEuler(nonFinite, outRadians));
}

TEST(QuaternionTest, RotateVector_PreservesFiniteExtremeHalfTurn) {
    const float maximum = std::numeric_limits<float>::max();
    const Vector3 result = RotateVector(Quaternion{0.0F, 0.0F, 1.0F, 0.0F},
                                               Vector3{maximum, -maximum, 0.0F});

    EXPECT_TRUE(std::isfinite(result.x));
    EXPECT_TRUE(std::isfinite(result.y));
    EXPECT_TRUE(std::isfinite(result.z));
    EXPECT_FLOAT_EQ(result.x, -maximum);
    EXPECT_FLOAT_EQ(result.y, maximum);
    EXPECT_FLOAT_EQ(result.z, 0.0F);
}

TEST(QuaternionTest, ToString_FormatsAllFourComponents) {
    const Quaternion rotation{0.0F, 0.0F, 0.0F, 1.0F};
    const std::string text = ToString(rotation);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
}

/// Sign-agnostic rotation comparison: q and -q are the same rotation, so comparing components
/// directly reports differences that do not exist. |dot| == 1 is the honest test.
[[nodiscard]] bool RepresentSameRotation(const Quaternion& lhs, const Quaternion& rhs,
                                            const float tolerance = 1e-4F) {
    const float dot = lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    return std::abs(std::abs(dot) - 1.0F) <= tolerance;
}

constexpr std::array<EulerOrder, 6U> kAllEulerOrders{
    EulerOrder::XYZ, EulerOrder::YXZ, EulerOrder::ZYX,
    EulerOrder::XZY, EulerOrder::YZX, EulerOrder::ZXY};

TEST(QuaternionEulerOrderTest, XYZIsBitForBitTheExistingTwoArgumentForm) {
    // The compatibility guarantee the whole feature rests on. TryMakeEuler has produced every
    // authored rotation in every saved scene; if the ordered form disagreed with it for XYZ, this
    // change would silently rotate existing content.
    const std::array<Vector3, 6U> samples{
        Vector3{0.1F, 0.2F, 0.3F},   Vector3{-0.8F, 1.0F, 0.26F},
        Vector3{1.4F, -0.5F, 2.1F},  Vector3{0.0F, 0.0F, 0.0F},
        Vector3{-2.9F, 0.35F, -1.6F}, Vector3{1.5708F, 0.7854F, 0.5236F}};

    for (const Vector3& radians : samples) {
        Quaternion viaExisting{};
        Quaternion viaOrdered{};
        ASSERT_TRUE(TryMakeEuler(radians, viaExisting));
        ASSERT_TRUE(TryMakeEulerOrdered(radians, EulerOrder::XYZ, viaOrdered));
        EXPECT_FLOAT_EQ(viaExisting.x, viaOrdered.x);
        EXPECT_FLOAT_EQ(viaExisting.y, viaOrdered.y);
        EXPECT_FLOAT_EQ(viaExisting.z, viaOrdered.z);
        EXPECT_FLOAT_EQ(viaExisting.w, viaOrdered.w);
    }
}

TEST(QuaternionEulerOrderTest, EveryOrderRoundTrips) {
    const std::array<Vector3, 5U> samples{
        Vector3{0.17F, 0.35F, 0.52F},  Vector3{-0.79F, 1.05F, 0.26F},
        Vector3{1.40F, -0.52F, 2.09F}, Vector3{0.09F, 0.09F, 0.09F},
        Vector3{-2.97F, 0.35F, -1.66F}};

    for (const EulerOrder order : kAllEulerOrders) {
        for (const Vector3& radians : samples) {
            Quaternion built{};
            ASSERT_TRUE(TryMakeEulerOrdered(radians, order, built))
                << "order " << static_cast<int>(order);
            Vector3 extracted{};
            ASSERT_TRUE(TryToEulerOrdered(built, order, extracted));
            Quaternion rebuilt{};
            ASSERT_TRUE(TryMakeEulerOrdered(extracted, order, rebuilt));
            // The ROTATION must round-trip. The angles need not be the same triple - away from a
            // singularity they will be, but asserting the rotation is what actually matters and
            // is what stays true at the poles.
            EXPECT_TRUE(RepresentSameRotation(built, rebuilt))
                << "order " << static_cast<int>(order);
        }
    }
}

TEST(QuaternionEulerOrderTest, EveryOrderSurvivesItsOwnGimbalLockPose) {
    // Each order has a different singular pose - that is the entire reason six exist. At the pole
    // the angle split is ambiguous, but the rotation must still rebuild exactly, or switching
    // order would corrupt an object rather than just relabel its angles.
    const std::array<Vector3, 6U> singularPoses{
        Vector3{0.0F, 1.5707963F, 0.0F},  // XYZ: middle axis is Y
        Vector3{1.5707963F, 0.0F, 0.0F},  // YXZ: middle axis is X
        Vector3{0.0F, 1.5707963F, 0.0F},  // ZYX
        Vector3{0.0F, 0.0F, 1.5707963F},  // XZY
        Vector3{0.0F, 0.0F, 1.5707963F},  // YZX
        Vector3{1.5707963F, 0.0F, 0.0F}}; // ZXY

    for (std::size_t index = 0U; index < kAllEulerOrders.size(); ++index) {
        const EulerOrder order = kAllEulerOrders[index];
        Quaternion built{};
        ASSERT_TRUE(TryMakeEulerOrdered(singularPoses[index], order, built));
        Vector3 extracted{};
        ASSERT_TRUE(TryToEulerOrdered(built, order, extracted));
        Quaternion rebuilt{};
        ASSERT_TRUE(TryMakeEulerOrdered(extracted, order, rebuilt));
        EXPECT_TRUE(RepresentSameRotation(built, rebuilt))
            << "order " << static_cast<int>(order) << " lost the rotation at its singular pose";
    }
}

TEST(QuaternionEulerOrderTest, OrdersProduceDifferentRotationsFromTheSameAngles) {
    // If two orders agreed on a general pose, one of them would be misimplemented and the
    // dropdown would be decorative. Axis-aligned angles are excluded deliberately: those SHOULD
    // agree, and using them here would make the test pass for the wrong reason.
    const Vector3 radians{0.6F, 0.9F, 1.2F};
    std::array<Quaternion, 6U> built{};
    for (std::size_t index = 0U; index < kAllEulerOrders.size(); ++index) {
        ASSERT_TRUE(TryMakeEulerOrdered(radians, kAllEulerOrders[index], built[index]));
    }
    for (std::size_t lhs = 0U; lhs < built.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1U; rhs < built.size(); ++rhs) {
            EXPECT_FALSE(RepresentSameRotation(built[lhs], built[rhs]))
                << "orders " << lhs << " and " << rhs << " are indistinguishable";
        }
    }
}

TEST(QuaternionEulerOrderTest, NonFiniteInputIsRejectedForEveryOrder) {
    const Vector3 notFinite{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
    const Quaternion zeroLength{0.0F, 0.0F, 0.0F, 0.0F};
    for (const EulerOrder order : kAllEulerOrders) {
        Quaternion rotation{};
        EXPECT_FALSE(TryMakeEulerOrdered(notFinite, order, rotation));
        Vector3 radians{};
        EXPECT_FALSE(TryToEulerOrdered(zeroLength, order, radians))
            << "a zero-length quaternion has no rotation to extract";
    }
}

TEST(QuaternionEulerOrderTest, AnUnknownOrderValueIsRejectedRatherThanSilentlyTreatedAsXYZ) {
    // The enum crosses a file format and an Inspector dropdown, so a value outside the six can
    // arrive from a hand-edited scene. Falling through to XYZ would silently rotate the object;
    // refusing leaves the caller's output untouched and the failure visible.
    const auto bogus = static_cast<EulerOrder>(200);
    Quaternion rotation{1.0F, 0.0F, 0.0F, 0.0F};
    const Quaternion before = rotation;
    EXPECT_FALSE(TryMakeEulerOrdered(Vector3{0.1F, 0.2F, 0.3F}, bogus, rotation));
    EXPECT_FLOAT_EQ(rotation.x, before.x) << "a rejected call must not write its output";

    Vector3 radians{7.0F, 7.0F, 7.0F};
    const Vector3 radiansBefore = radians;
    EXPECT_FALSE(TryToEulerOrdered(Quaternion{0.0F, 0.0F, 0.0F, 1.0F}, bogus, radians));
    EXPECT_FLOAT_EQ(radians.x, radiansBefore.x);
}

} // namespace
} // namespace UVE::Math::Tests
