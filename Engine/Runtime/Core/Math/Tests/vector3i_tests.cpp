// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "vector3i.h"

#include <cstdint>
#include <string>

#include "vector3.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(Vector3iTest, DefaultConstruction_IsZero) {
    constexpr Vector3i v{};
    EXPECT_EQ(v.x, 0);
    EXPECT_EQ(v.y, 0);
    EXPECT_EQ(v.z, 0);
}

TEST(Vector3iTest, Arithmetic_AddSubNegateScale) {
    constexpr Vector3i a{10, -3, 5};
    constexpr Vector3i b{4, 7, -2};
    EXPECT_EQ(a + b, (Vector3i{14, 4, 3}));
    EXPECT_EQ(a - b, (Vector3i{6, -10, 7}));
    EXPECT_EQ(-a, (Vector3i{-10, 3, -5}));
    EXPECT_EQ(a * 3, (Vector3i{30, -9, 15}));
}

TEST(Vector3iTest, EqualityOperators_CompareAllComponents) {
    constexpr Vector3i a{1, 2, 3};
    constexpr Vector3i b{1, 2, 3};
    constexpr Vector3i c{1, 2, 4};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(Vector3iTest, Dot_WidensToInt64WithoutOverflow) {
    // 3 * 50000^2 = 7.5e9: exact in int64, overflowed in int32.
    constexpr Vector3i v{50000, 50000, 50000};
    EXPECT_EQ(Dot(v, v), 7500000000LL);
    EXPECT_EQ(LengthSquared(v), 7500000000LL);
}

TEST(Vector3iTest, Length_OneTwoTwoIsThree) {
    EXPECT_FLOAT_EQ(Length(Vector3i{1, 2, 2}), 3.0F);
}

TEST(Vector3iTest, MinMaxClamp_AreComponentWise) {
    constexpr Vector3i a{1, 9, -5};
    constexpr Vector3i b{5, 3, 0};
    EXPECT_EQ(Min(a, b), (Vector3i{1, 3, -5}));
    EXPECT_EQ(Max(a, b), (Vector3i{5, 9, 0}));
    EXPECT_EQ(Clamp(Vector3i{-4, 99, 7}, Vector3i{0, 0, 0}, Vector3i{10, 10, 10}),
              (Vector3i{0, 10, 7}));
}

TEST(Vector3iTest, ToVector3_ConvertsComponents) {
    constexpr Vector3i v{-7, 42, 0};
    const Vector3 f = ToVector3(v);
    EXPECT_FLOAT_EQ(f.x, -7.0F);
    EXPECT_FLOAT_EQ(f.y, 42.0F);
    EXPECT_FLOAT_EQ(f.z, 0.0F);
}

TEST(Vector3iTest, ToString_FormatsAllComponents) {
    EXPECT_EQ(ToString(Vector3i{-7, 42, 0}), "(-7, 42, 0)");
}

} // namespace
} // namespace UVE::Math::Tests
