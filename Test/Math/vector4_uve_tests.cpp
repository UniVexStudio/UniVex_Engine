// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector4_uve.h"

#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

constexpr float kEpsilon = 1e-5F;

TEST(Vector4UVETest, DefaultConstruction_IsZero) {
    constexpr Vector4UVE vector{};
    EXPECT_EQ(vector.x, 0.0F);
    EXPECT_EQ(vector.y, 0.0F);
    EXPECT_EQ(vector.z, 0.0F);
    EXPECT_EQ(vector.w, 0.0F);
}

TEST(Vector4UVETest, Addition_SumsEachComponent) {
    constexpr Vector4UVE lhs{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4UVE rhs{10.0F, 20.0F, 30.0F, 40.0F};
    constexpr Vector4UVE sum = lhs + rhs;

    EXPECT_EQ(sum.x, 11.0F);
    EXPECT_EQ(sum.y, 22.0F);
    EXPECT_EQ(sum.z, 33.0F);
    EXPECT_EQ(sum.w, 44.0F);
}

TEST(Vector4UVETest, ComponentWiseMultiplication_MultipliesEachComponent) {
    constexpr Vector4UVE lhs{2.0F, 3.0F, 4.0F, 5.0F};
    constexpr Vector4UVE rhs{5.0F, 6.0F, 7.0F, 8.0F};
    constexpr Vector4UVE product = lhs * rhs;

    EXPECT_EQ(product.x, 10.0F);
    EXPECT_EQ(product.y, 18.0F);
    EXPECT_EQ(product.z, 28.0F);
    EXPECT_EQ(product.w, 40.0F);
}

TEST(Vector4UVETest, EqualityOperators_CompareAllComponents) {
    constexpr Vector4UVE a{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4UVE b{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4UVE c{1.0F, 2.0F, 3.0F, 5.0F};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

TEST(Vector4UVETest, Subtraction_DiffsEachComponent) {
    constexpr Vector4UVE lhs{10.0F, 20.0F, 30.0F, 40.0F};
    constexpr Vector4UVE rhs{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4UVE diff = lhs - rhs;

    EXPECT_EQ(diff.x, 9.0F);
    EXPECT_EQ(diff.y, 18.0F);
    EXPECT_EQ(diff.z, 27.0F);
    EXPECT_EQ(diff.w, 36.0F);
}

TEST(Vector4UVETest, UnaryNegate_FlipsEachComponent) {
    constexpr Vector4UVE v{1.0F, -2.0F, 3.0F, -4.0F};
    constexpr Vector4UVE negated = -v;

    EXPECT_EQ(negated.x, -1.0F);
    EXPECT_EQ(negated.y, 2.0F);
    EXPECT_EQ(negated.z, -3.0F);
    EXPECT_EQ(negated.w, 4.0F);
}

TEST(Vector4UVETest, ScalarMultiplication_ScalesEachComponent) {
    constexpr Vector4UVE v{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4UVE scaled = v * 2.5F;

    EXPECT_EQ(scaled.x, 2.5F);
    EXPECT_EQ(scaled.y, 5.0F);
    EXPECT_EQ(scaled.z, 7.5F);
    EXPECT_EQ(scaled.w, 10.0F);
}

TEST(Vector4UVETest, CompoundAssignmentOperators_MutateInPlace) {
    Vector4UVE v{1.0F, 2.0F, 3.0F, 4.0F};

    v += Vector4UVE{10.0F, 20.0F, 30.0F, 40.0F};
    EXPECT_EQ(v, (Vector4UVE{11.0F, 22.0F, 33.0F, 44.0F}));

    v -= Vector4UVE{1.0F, 2.0F, 3.0F, 4.0F};
    EXPECT_EQ(v, (Vector4UVE{10.0F, 20.0F, 30.0F, 40.0F}));

    v *= 2.0F;
    EXPECT_EQ(v, (Vector4UVE{20.0F, 40.0F, 60.0F, 80.0F}));
}

TEST(Vector4UVETest, DotUVE_KnownOrthogonalVectors_IsZero) {
    constexpr Vector4UVE right{1.0F, 0.0F, 0.0F, 0.0F};
    constexpr Vector4UVE up{0.0F, 1.0F, 0.0F, 0.0F};
    EXPECT_EQ(DotUVE(right, up), 0.0F);
}

TEST(Vector4UVETest, DotUVE_KnownVectors_MatchesHandComputedValue) {
    constexpr Vector4UVE lhs{1.0F, 2.0F, 3.0F, 4.0F};
    constexpr Vector4UVE rhs{5.0F, 6.0F, 7.0F, 8.0F};
    EXPECT_EQ(DotUVE(lhs, rhs), 70.0F); // 1*5 + 2*6 + 3*7 + 4*8
}

TEST(Vector4UVETest, DotUVE_PreservesFiniteExtremeCancellation) {
    const float maximum = std::numeric_limits<float>::max();
    const float half = 0.5F;
    const Vector4UVE lhs{maximum, maximum, maximum, maximum};
    const Vector4UVE rhs{half, half, half, -half};

    const float result = DotUVE(lhs, rhs);

    EXPECT_TRUE(std::isfinite(result));
    EXPECT_FLOAT_EQ(result, maximum);
}

TEST(Vector4UVETest, LengthUVE_KnownVector_MatchesHandComputedValue) {
    const Vector4UVE v{1.0F, 2.0F, 2.0F, 4.0F};
    EXPECT_NEAR(LengthUVE(v), 5.0F, kEpsilon);
    EXPECT_NEAR(LengthSquaredUVE(v), 25.0F, kEpsilon);
}

TEST(Vector4UVETest, LengthUVE_PreservesFiniteMaximumAxis) {
    const float maximum = std::numeric_limits<float>::max();

    EXPECT_TRUE(std::isfinite(LengthUVE(Vector4UVE{maximum, 0.0F, 0.0F, 0.0F})));
    EXPECT_FLOAT_EQ(LengthUVE(Vector4UVE{maximum, 0.0F, 0.0F, 0.0F}), maximum);
}

TEST(Vector4UVETest, NormalizeUVE_NonZeroVector_ProducesUnitLength) {
    const Vector4UVE v{1.0F, 2.0F, 2.0F, 4.0F};
    const Vector4UVE normalized = NormalizeUVE(v);

    EXPECT_NEAR(LengthUVE(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.2F, kEpsilon);
    EXPECT_NEAR(normalized.y, 0.4F, kEpsilon);
    EXPECT_NEAR(normalized.z, 0.4F, kEpsilon);
    EXPECT_NEAR(normalized.w, 0.8F, kEpsilon);
}

TEST(Vector4UVETest, NormalizeUVE_LargeFiniteVector_ProducesUnitLength) {
    const float maximum = std::numeric_limits<float>::max();
    const Vector4UVE normalized = NormalizeUVE(Vector4UVE{maximum, maximum, maximum, maximum});
    EXPECT_TRUE(std::isfinite(normalized.x));
    EXPECT_TRUE(std::isfinite(normalized.y));
    EXPECT_TRUE(std::isfinite(normalized.z));
    EXPECT_TRUE(std::isfinite(normalized.w));
    EXPECT_NEAR(LengthSquaredUVE(normalized), 1.0F, kEpsilon);
    EXPECT_NEAR(normalized.x, 0.5F, 1.0e-6F);
    EXPECT_NEAR(normalized.y, 0.5F, 1.0e-6F);
    EXPECT_NEAR(normalized.z, 0.5F, 1.0e-6F);
    EXPECT_NEAR(normalized.w, 0.5F, 1.0e-6F);
}

TEST(Vector4UVETest, NormalizeUVE_ZeroVector_ProducesInfRatherThanTrapping) {
    // Documents NormalizeUVE()'s zero-length contract (see its doc comment): callers must not
    // pass the zero vector, but IEEE754 float division by zero produces +/-inf, not a crash —
    // this test pins down that actual (not just documented) behavior.
    const Vector4UVE normalized = NormalizeUVE(Vector4UVE{});
    EXPECT_TRUE(std::isinf(normalized.x) || std::isnan(normalized.x));
}

TEST(Vector4UVETest, IsFiniteUVE_DetectsNonFiniteComponents) {
    EXPECT_TRUE(IsFiniteUVE(Vector4UVE{1.0F, 2.0F, 3.0F, 4.0F}));
    EXPECT_FALSE(IsFiniteUVE(Vector4UVE{std::numeric_limits<float>::infinity(), 0.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(IsFiniteUVE(Vector4UVE{0.0F, 0.0F, 0.0F, std::numeric_limits<float>::quiet_NaN()}));
}

TEST(Vector4UVETest, ToStringUVE_FormatsAllFourComponents) {
    const Vector4UVE vector{1.0F, 2.0F, 3.0F, 4.0F};
    const std::string text = ToStringUVE(vector);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("2.000000"), std::string::npos);
    EXPECT_NE(text.find("3.000000"), std::string::npos);
    EXPECT_NE(text.find("4.000000"), std::string::npos);
}

} // namespace
} // namespace UVE::Math::Tests
