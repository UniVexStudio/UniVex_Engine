// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector4.h"

#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(Vector4Test, DefaultConstruction_IsZero) {
    constexpr Vector4 vector{};
    EXPECT_EQ(vector.x, 0.0F);
    EXPECT_EQ(vector.y, 0.0F);
    EXPECT_EQ(vector.z, 0.0F);
    EXPECT_EQ(vector.w, 0.0F);
}

TEST(Vector4Test, Addition_SumsEachComponent) {
    constexpr Vector4 lhs{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4 rhs{10.0F, 20.0F, 30.0F, 40.0F};
    constexpr Vector4 sum = lhs + rhs;

    EXPECT_EQ(sum.x, 11.0F);
    EXPECT_EQ(sum.y, 22.0F);
    EXPECT_EQ(sum.z, 33.0F);
    EXPECT_EQ(sum.w, 44.0F);
}

TEST(Vector4Test, ComponentWiseMultiplication_MultipliesEachComponent) {
    constexpr Vector4 lhs{2.0F, 3.0F, 4.0F, 5.0F};
    constexpr Vector4 rhs{5.0F, 6.0F, 7.0F, 8.0F};
    constexpr Vector4 product = lhs * rhs;

    EXPECT_EQ(product.x, 10.0F);
    EXPECT_EQ(product.y, 18.0F);
    EXPECT_EQ(product.z, 28.0F);
    EXPECT_EQ(product.w, 40.0F);
}

TEST(Vector4Test, EqualityOperators_CompareAllComponents) {
    constexpr Vector4 a{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4 b{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4 c{1.0F, 2.0F, 3.0F, 5.0F};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(Vector4Test, Subtraction_DiffsEachComponent) {
    constexpr Vector4 lhs{10.0F, 20.0F, 30.0F, 40.0F};
    constexpr Vector4 rhs{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4 diff = lhs - rhs;

    EXPECT_EQ(diff.x, 9.0F);
    EXPECT_EQ(diff.y, 18.0F);
    EXPECT_EQ(diff.z, 27.0F);
    EXPECT_EQ(diff.w, 36.0F);
}

TEST(Vector4Test, UnaryNegate_FlipsEachComponent) {
    constexpr Vector4 v{1.0F, -2.0F, 3.0F, -4.0F};
    constexpr Vector4 negated = -v;

    EXPECT_EQ(negated.x, -1.0F);
    EXPECT_EQ(negated.y, 2.0F);
    EXPECT_EQ(negated.z, -3.0F);
    EXPECT_EQ(negated.w, 4.0F);
}

TEST(Vector4Test, ScalarMultiplication_ScalesEachComponent) {
    constexpr Vector4 v{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4 scaled = v * 2.5F;

    EXPECT_EQ(scaled.x, 2.5F);
    EXPECT_EQ(scaled.y, 5.0F);
    EXPECT_EQ(scaled.z, 7.5F);
    EXPECT_EQ(scaled.w, 10.0F);
}

TEST(Vector4Test, CompoundAssignmentOperators_MutateInPlace) {
    Vector4 v{1.0F, 2.0F, 3.0F, 4.0F};

    v += Vector4{10.0F, 20.0F, 30.0F, 40.0F};
    EXPECT_EQ(v, (Vector4{11.0F, 22.0F, 33.0F, 44.0F}));

    v -= Vector4{1.0F, 2.0F, 3.0F, 4.0F};
    EXPECT_EQ(v, (Vector4{10.0F, 20.0F, 30.0F, 40.0F}));

    v *= 2.0F;
    EXPECT_EQ(v, (Vector4{20.0F, 40.0F, 60.0F, 80.0F}));
}

TEST(Vector4Test, Dot_KnownOrthogonalVectors_IsZero) {
    constexpr Vector4 right{1.0F, 0.0F, 0.0F, 0.0F};
    constexpr Vector4 up{0.0F, 1.0F, 0.0F, 0.0F};
    EXPECT_EQ(Dot(right, up), 0.0F);
}

TEST(Vector4Test, Dot_KnownVectors_MatchesHandComputedValue) {
    constexpr Vector4 lhs{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4 rhs{5.0F, 6.0F, 7.0F, 8.0F};
    EXPECT_EQ(Dot(lhs, rhs), 70.0F); // 1*5 + 2*6 + 3*7 + 4*8
}

TEST(Vector4Test, Dot_PreservesFiniteExtremeCancellation) {
    const float maximum = std::numeric_limits<float>::max();
    const float half = 0.5F;
    const Vector4 lhs{maximum, maximum, maximum, maximum};
    const Vector4 rhs{half, half, half, -half};

    const float result = Dot(lhs, rhs);

    EXPECT_TRUE(std::isfinite(result));
    EXPECT_FLOAT_EQ(result, maximum);
}

TEST(Vector4Test, Length_KnownVector_MatchesHandComputedValue) {
    const Vector4 v{1.0F, 2.0F, 2.0F, 4.0F};
    EXPECT_NEAR(Length(v), 5.0F, kEpsilon);
    EXPECT_NEAR(LengthSquared(v), 25.0F, kEpsilon);
}

TEST(Vector4Test, Length_PreservesFiniteMaximumAxis) {
    const float maximum = std::numeric_limits<float>::max();

    EXPECT_TRUE(std::isfinite(Length(Vector4{maximum, 0.0F, 0.0F, 0.0F})));
    EXPECT_FLOAT_EQ(Length(Vector4{maximum, 0.0F, 0.0F, 0.0F}), maximum);
}

TEST(Vector4Test, Normalize_NonZeroVector_ProducesUnitLength) {
    const Vector4 v{1.0F, 2.0F, 2.0F, 4.0F};
    const Vector4 normalized = Normalize(v);

    EXPECT_NEAR(Length(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.2F, kEpsilon);
    EXPECT_NEAR(normalized.y, 0.4F, kEpsilon);
    EXPECT_NEAR(normalized.z, 0.4F, kEpsilon);
    EXPECT_NEAR(normalized.w, 0.8F, kEpsilon);
}

TEST(Vector4Test, Normalize_LargeFiniteVector_ProducesUnitLength) {
    const float maximum = std::numeric_limits<float>::max();
    const Vector4 normalized = Normalize(Vector4{maximum, maximum, maximum, maximum});
    EXPECT_TRUE(std::isfinite(normalized.x));
    EXPECT_TRUE(std::isfinite(normalized.y));
    EXPECT_TRUE(std::isfinite(normalized.z));
    EXPECT_TRUE(std::isfinite(normalized.w));
    EXPECT_NEAR(LengthSquared(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.5F, 1.0e-6F);
    EXPECT_NEAR(normalized.y, 0.5F, 1.0e-6F);
    EXPECT_NEAR(normalized.z, 0.5F, 1.0e-6F);
    EXPECT_NEAR(normalized.w, 0.5F, 1.0e-6F);
}

TEST(Vector4Test, Normalize_ZeroVector_ProducesInfRatherThanTrapping) {
    // Documents Normalize()'s zero-length contract (see its doc comment): callers must not
    // pass the zero vector, but IEEE754 float division by zero produces +/-inf, not a crash —
    // this test pins down that actual (not just documented) behavior.
    const Vector4 normalized = Normalize(Vector4{});
    EXPECT_TRUE(std::isinf(normalized.x) || std::isnan(normalized.x));
}

TEST(Vector4Test, IsFinite_DetectsNonFiniteComponents) {
    EXPECT_TRUE(IsFinite(Vector4{1.0F, 2.0F, 3.0F, 4.0F}));
    EXPECT_FALSE(IsFinite(Vector4{std::numeric_limits<float>::infinity(), 0.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(IsFinite(Vector4{0.0F, 0.0F, 0.0F, std::numeric_limits<float>::quiet_NaN()}));
}

TEST(Vector4Test, ToString_FormatsAllFourComponents) {
    const Vector4 vector{1.0F, 2.0F, 3.0F, 4.0F};
    const std::string text = ToString(vector);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("2.000000"), std::string::npos);
    EXPECT_NE(text.find("3.000000"), std::string::npos);
    EXPECT_NE(text.find("4.000000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
