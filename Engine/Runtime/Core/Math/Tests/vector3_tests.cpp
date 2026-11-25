// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector3.h"

#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(Vector3Test, DefaultConstruction_IsZero) {
    constexpr Vector3 vector{};
    EXPECT_EQ(vector.x, 0.0F);
    EXPECT_EQ(vector.y, 0.0F);
    EXPECT_EQ(vector.z, 0.0F);
}

TEST(Vector3Test, Addition_SumsEachComponent) {
    constexpr Vector3 lhs{1.0F, 2.0F, 3.0F};
    constexpr Vector3 rhs{10.0F, 20.0F, 30.0F};
    constexpr Vector3 sum = lhs + rhs;

    EXPECT_EQ(sum.x, 11.0F);
    EXPECT_EQ(sum.y, 22.0F);
    EXPECT_EQ(sum.z, 33.0F);
}

TEST(Vector3Test, ComponentWiseMultiplication_MultipliesEachComponent) {
    constexpr Vector3 lhs{2.0F, 3.0F, 4.0F};
    constexpr Vector3 rhs{5.0F, 6.0F, 7.0F};
    constexpr Vector3 product = lhs * rhs;

    EXPECT_EQ(product.x, 10.0F);
    EXPECT_EQ(product.y, 18.0F);
    EXPECT_EQ(product.z, 28.0F);
}

TEST(Vector3Test, EqualityOperators_CompareAllComponents) {
    constexpr Vector3 a{1.0F, 2.0F, 3.0F};
    constexpr Vector3 b{1.0F, 2.0F, 3.0F};
    constexpr Vector3 c{1.0F, 2.0F, 4.0F};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(Vector3Test, Subtraction_DiffsEachComponent) {
    constexpr Vector3 lhs{10.0F, 20.0F, 30.0F};
    constexpr Vector3 rhs{1.0F, 2.0F, 3.0F};
    constexpr Vector3 diff = lhs - rhs;

    EXPECT_EQ(diff.x, 9.0F);
    EXPECT_EQ(diff.y, 18.0F);
    EXPECT_EQ(diff.z, 27.0F);
}

TEST(Vector3Test, UnaryNegate_FlipsEachComponent) {
    constexpr Vector3 v{1.0F, -2.0F, 3.0F};
    constexpr Vector3 negated = -v;

    EXPECT_EQ(negated.x, -1.0F);
    EXPECT_EQ(negated.y, 2.0F);
    EXPECT_EQ(negated.z, -3.0F);
}

TEST(Vector3Test, ScalarMultiplication_ScalesEachComponent) {
    constexpr Vector3 v{1.0F, 2.0F, 3.0F};
    constexpr Vector3 scaled = v * 2.0F;

    EXPECT_EQ(scaled.x, 2.0F);
    EXPECT_EQ(scaled.y, 4.0F);
    EXPECT_EQ(scaled.z, 6.0F);
}

TEST(Vector3Test, CompoundAssignmentOperators_MutateInPlace) {
    Vector3 v{1.0F, 2.0F, 3.0F};
    v += Vector3{1.0F, 1.0F, 1.0F};
    EXPECT_EQ(v, (Vector3{2.0F, 3.0F, 4.0F}));

    v -= Vector3{1.0F, 1.0F, 1.0F};
    EXPECT_EQ(v, (Vector3{1.0F, 2.0F, 3.0F}));

    v *= 2.0F;
    EXPECT_EQ(v, (Vector3{2.0F, 4.0F, 6.0F}));
}

TEST(Vector3Test, Dot_KnownOrthogonalVectors_IsZero) {
    constexpr Vector3 right{1.0F, 0.0F, 0.0F};
    constexpr Vector3 up{0.0F, 1.0F, 0.0F};
    EXPECT_EQ(Dot(right, up), 0.0F);
}

TEST(Vector3Test, Dot_KnownVectors_MatchesHandComputedValue) {
    constexpr Vector3 lhs{1.0F, 2.0F, 3.0F};
    constexpr Vector3 rhs{4.0F, 5.0F, 6.0F};
    EXPECT_EQ(Dot(lhs, rhs), 32.0F); // 1*4 + 2*5 + 3*6
}

TEST(Vector3Test, Dot_PreservesFiniteExtremeCancellation) {
    const float maximum = std::numeric_limits<float>::max();
    const float diagonal = 0.57735026919F;
    const Vector3 lhs{maximum, maximum, maximum};
    const Vector3 rhs{diagonal, diagonal, -diagonal};

    const float result = Dot(lhs, rhs);

    EXPECT_TRUE(std::isfinite(result));
    EXPECT_FLOAT_EQ(result, diagonal * maximum);
}

TEST(Vector3Test, Cross_RightCrossUp_IsForward) {
    constexpr Vector3 right{1.0F, 0.0F, 0.0F};
    constexpr Vector3 up{0.0F, 1.0F, 0.0F};
    constexpr Vector3 forward = Cross(right, up);

    EXPECT_EQ(forward, (Vector3{0.0F, 0.0F, 1.0F}));
}

TEST(Vector3Test, Length_KnownVector_MatchesHandComputedValue) {
    const Vector3 v{3.0F, 4.0F, 0.0F};
    EXPECT_NEAR(Length(v), 5.0F, kEpsilon);
    EXPECT_NEAR(LengthSquared(v), 25.0F, kEpsilon);
}

TEST(Vector3Test, Length_PreservesFiniteMaximumAxis) {
    const float maximum = std::numeric_limits<float>::max();

    EXPECT_TRUE(std::isfinite(Length(Vector3{maximum, 0.0F, 0.0F})));
    EXPECT_FLOAT_EQ(Length(Vector3{maximum, 0.0F, 0.0F}), maximum);
}

TEST(Vector3Test, Normalize_NonZeroVector_ProducesUnitLength) {
    const Vector3 v{3.0F, 4.0F, 0.0F};
    const Vector3 normalized = Normalize(v);

    EXPECT_NEAR(Length(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.6F, kEpsilon);
    EXPECT_NEAR(normalized.y, 0.8F, kEpsilon);
}

TEST(Vector3Test, Normalize_LargeFiniteVector_ProducesUnitLength) {
    const float maximum = std::numeric_limits<float>::max();
    const Vector3 normalized = Normalize(Vector3{maximum, maximum, 0.0F});
    EXPECT_TRUE(std::isfinite(normalized.x));
    EXPECT_TRUE(std::isfinite(normalized.y));
    EXPECT_TRUE(std::isfinite(normalized.z));
    EXPECT_NEAR(LengthSquared(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.70710677F, 1.0e-6F);
    EXPECT_NEAR(normalized.y, 0.70710677F, 1.0e-6F);
}

TEST(Vector3Test, Normalize_ZeroVector_ProducesInfRatherThanTrapping) {
    // Documents Normalize()'s zero-length contract (see its doc comment): callers must not
    // pass the zero vector, but IEEE754 float division by zero produces +/-inf, not a crash —
    // this test pins down that actual (not just documented) behavior.
    const Vector3 normalized = Normalize(Vector3{});
    EXPECT_TRUE(std::isinf(normalized.x) || std::isnan(normalized.x));
}

TEST(Vector3Test, ToString_FormatsAllThreeComponents) {
    const Vector3 vector{1.0F, 2.0F, 3.0F};
    const std::string text = ToString(vector);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("2.000000"), std::string::npos);
    EXPECT_NE(text.find("3.000000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
