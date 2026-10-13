// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector2_uve.h"

#include <limits>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(Vector2UVETest, DefaultConstruction_IsZero) {
    constexpr Vector2UVE vector{};
    EXPECT_EQ(vector.x, 0.0F);
    EXPECT_EQ(vector.y, 0.0F);
}

TEST(Vector2UVETest, Addition_SumsEachComponent) {
    constexpr Vector2UVE lhs{1.0F, 2.0F};
    constexpr Vector2UVE rhs{10.0F, 20.0F};
    constexpr Vector2UVE sum = lhs + rhs;

    EXPECT_EQ(sum.x, 11.0F);
    EXPECT_EQ(sum.y, 22.0F);
}

TEST(Vector2UVETest, Subtraction_DiffsEachComponent) {
    constexpr Vector2UVE lhs{10.0F, 20.0F};
    constexpr Vector2UVE rhs{1.0F, 2.0F};
    constexpr Vector2UVE diff = lhs - rhs;

    EXPECT_EQ(diff.x, 9.0F);
    EXPECT_EQ(diff.y, 18.0F);
}

TEST(Vector2UVETest, EqualityOperators_CompareBothComponents) {
    constexpr Vector2UVE a{1.0F, 2.0F};
    constexpr Vector2UVE b{1.0F, 2.0F};
    constexpr Vector2UVE c{1.0F, 3.0F};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a != b);
}

TEST(Vector2UVETest, ToStringUVE_ContainsBothComponents) {
    const Vector2UVE vector{1.0F, 2.0F};
    const std::string text = ToStringUVE(vector);

    EXPECT_NE(text.find("1.000000"), std::string::npos);
    EXPECT_NE(text.find("2.000000"), std::string::npos);
}

TEST(Vector2UVETest, Dot_SumsComponentProducts) {
    constexpr Vector2UVE lhs{1.0F, 2.0F};
    constexpr Vector2UVE rhs{10.0F, 20.0F};
    EXPECT_FLOAT_EQ(DotUVE(lhs, rhs), 50.0F);
    EXPECT_FLOAT_EQ(DotUVE(lhs, Vector2UVE{-2.0F, 1.0F}), 0.0F);
}

TEST(Vector2UVETest, LengthSquared_AgreesWithDot) {
    constexpr Vector2UVE vector{3.0F, 4.0F};
    EXPECT_FLOAT_EQ(LengthSquaredUVE(vector), 25.0F);
    EXPECT_FLOAT_EQ(LengthSquaredUVE(vector), DotUVE(vector, vector));
}

TEST(Vector2UVETest, Length_MeasuresEuclideanDistance) {
    EXPECT_FLOAT_EQ(LengthUVE(Vector2UVE{3.0F, 4.0F}), 5.0F);
    EXPECT_FLOAT_EQ(LengthUVE(Vector2UVE{}), 0.0F);
}

TEST(Vector2UVETest, Normalize_ScalesToUnitLength) {
    const Vector2UVE unit = NormalizeUVE(Vector2UVE{3.0F, 4.0F});
    EXPECT_FLOAT_EQ(LengthUVE(unit), 1.0F);
    EXPECT_FLOAT_EQ(unit.x, 0.6F);
    EXPECT_FLOAT_EQ(unit.y, 0.8F);
}

TEST(Vector2UVETest, IsFinite_RejectsNonFiniteComponents) {
    EXPECT_TRUE(IsFiniteUVE(Vector2UVE{1.0F, 2.0F}));
    EXPECT_FALSE(IsFiniteUVE(Vector2UVE{std::numeric_limits<float>::infinity(), 0.0F}));
    EXPECT_FALSE(IsFiniteUVE(Vector2UVE{0.0F, std::numeric_limits<float>::quiet_NaN()}));
}

} // namespace
} // namespace UVE::Math::Tests
