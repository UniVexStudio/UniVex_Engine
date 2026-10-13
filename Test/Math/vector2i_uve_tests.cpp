// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/vector2i_uve.h"

#include <cstdint>
#include <string>

#include "uve/math/vector2_uve.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(Vector2iUVETest, DefaultConstruction_IsZero) {
    constexpr Vector2iUVE v{};
    EXPECT_EQ(v.x, 0);
    EXPECT_EQ(v.y, 0);
}

TEST(Vector2iUVETest, Arithmetic_AddSubNegateScale) {
    constexpr Vector2iUVE a{10, -3};
    constexpr Vector2iUVE b{4, 7};
    EXPECT_EQ(a + b, (Vector2iUVE{14, 4}));
    EXPECT_EQ(a - b, (Vector2iUVE{6, -10}));
    EXPECT_EQ(-a, (Vector2iUVE{-10, 3}));
    EXPECT_EQ(a * 3, (Vector2iUVE{30, -9}));
}

TEST(Vector2iUVETest, EqualityOperators_CompareBothComponents) {
    constexpr Vector2iUVE a{1, 2};
    constexpr Vector2iUVE b{1, 2};
    constexpr Vector2iUVE c{1, 3};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(Vector2iUVETest, Dot_WidensToInt64WithoutOverflow) {
    // 50000^2 * 2 = 5e9: exact in int64, overflowed in int32.
    constexpr Vector2iUVE v{50000, 50000};
    EXPECT_EQ(DotUVE(v, v), 5000000000LL);
    constexpr Vector2iUVE mixed{-50000, 20000};
    EXPECT_EQ(DotUVE(v, mixed), -1500000000LL);
    EXPECT_EQ(LengthSquaredUVE(v), 5000000000LL);
}

TEST(Vector2iUVETest, Length_ThreeFourFive) {
    EXPECT_FLOAT_EQ(LengthUVE(Vector2iUVE{3, 4}), 5.0F);
    EXPECT_FLOAT_EQ(LengthUVE(Vector2iUVE{0, 0}), 0.0F);
}

TEST(Vector2iUVETest, MinMaxClamp_AreComponentWise) {
    constexpr Vector2iUVE a{1, 9};
    constexpr Vector2iUVE b{5, 3};
    EXPECT_EQ(MinUVE(a, b), (Vector2iUVE{1, 3}));
    EXPECT_EQ(MaxUVE(a, b), (Vector2iUVE{5, 9}));
    EXPECT_EQ(ClampUVE(Vector2iUVE{-4, 99}, Vector2iUVE{0, 0}, Vector2iUVE{10, 10}),
              (Vector2iUVE{0, 10}));
}

TEST(Vector2iUVETest, ToVector2_ConvertsComponents) {
    constexpr Vector2iUVE v{-7, 42};
    const Vector2UVE f = ToVector2UVE(v);
    EXPECT_FLOAT_EQ(f.x, -7.0F);
    EXPECT_FLOAT_EQ(f.y, 42.0F);
}

TEST(Vector2iUVETest, ToString_FormatsBothComponents) {
    EXPECT_EQ(ToStringUVE(Vector2iUVE{-7, 42}), "(-7, 42)");
}

} // namespace
} // namespace UVE::Math::Tests
