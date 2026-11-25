// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "rect.h"

#include <limits>
#include <string>

#include "vector2.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(RectTest, DefaultConstruction_IsEmptyAtOrigin) {
    constexpr Rect rect{};
    EXPECT_EQ(rect.position, (Vector2{0.0F, 0.0F}));
    EXPECT_EQ(rect.size, (Vector2{0.0F, 0.0F}));
}

TEST(RectTest, EqualityOperators_ComparePositionAndSize) {
    constexpr Rect a{Vector2{1.0F, 2.0F}, Vector2{3.0F, 4.0F}};
    constexpr Rect b{Vector2{1.0F, 2.0F}, Vector2{3.0F, 4.0F}};
    constexpr Rect c{Vector2{1.0F, 2.0F}, Vector2{3.0F, 5.0F}};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(RectTest, ContainsPoint_EdgesAreIncluded) {
    // UIRuntime's hover contract: edge pixels count, so every comparison is inclusive.
    constexpr Rect rect{Vector2{10.0F, 20.0F}, Vector2{30.0F, 40.0F}};
    EXPECT_TRUE(Contains(rect, Vector2{10.0F, 20.0F}));
    EXPECT_TRUE(Contains(rect, Vector2{40.0F, 60.0F}));
    EXPECT_TRUE(Contains(rect, Vector2{25.0F, 60.0F}));
    EXPECT_TRUE(Contains(rect, Vector2{10.0F, 45.0F}));
    EXPECT_FALSE(Contains(rect, Vector2{9.999F, 45.0F}));
    EXPECT_FALSE(Contains(rect, Vector2{40.001F, 45.0F}));
    EXPECT_FALSE(Contains(rect, Vector2{25.0F, 60.001F}));
}

TEST(RectTest, ContainsRect_InnerMustFitWholly) {
    constexpr Rect outer{Vector2{0.0F, 0.0F}, Vector2{100.0F, 100.0F}};
    EXPECT_TRUE(Contains(outer, Rect{Vector2{10.0F, 10.0F}, Vector2{20.0F, 20.0F}}));
    EXPECT_TRUE(Contains(outer, outer));
    EXPECT_FALSE(Contains(outer, Rect{Vector2{90.0F, 90.0F}, Vector2{20.0F, 20.0F}}));
    EXPECT_FALSE(Contains(outer, Rect{Vector2{-1.0F, 0.0F}, Vector2{10.0F, 10.0F}}));
}

TEST(RectTest, Intersects_EdgeTouchingIsNotAnOverlap) {
    constexpr Rect a{Vector2{0.0F, 0.0F}, Vector2{10.0F, 10.0F}};
    EXPECT_TRUE(Intersects(a, Rect{Vector2{5.0F, 5.0F}, Vector2{10.0F, 10.0F}}));
    EXPECT_FALSE(Intersects(a, Rect{Vector2{10.0F, 0.0F}, Vector2{10.0F, 10.0F}}));
    EXPECT_FALSE(Intersects(a, Rect{Vector2{20.0F, 20.0F}, Vector2{5.0F, 5.0F}}));
}

TEST(RectTest, Intersection_ComputesOverlapAndEmptyForDisjoint) {
    constexpr Rect overlap =
        Intersection(Rect{Vector2{0.0F, 0.0F}, Vector2{10.0F, 10.0F}},
                        Rect{Vector2{5.0F, 5.0F}, Vector2{10.0F, 10.0F}});
    EXPECT_EQ(overlap, (Rect{Vector2{5.0F, 5.0F}, Vector2{5.0F, 5.0F}}));
    const Rect empty =
        Intersection(Rect{Vector2{0.0F, 0.0F}, Vector2{10.0F, 10.0F}},
                        Rect{Vector2{20.0F, 20.0F}, Vector2{5.0F, 5.0F}});
    EXPECT_EQ(empty.size, (Vector2{0.0F, 0.0F}));
}

TEST(RectTest, Union_SpansBothRects) {
    constexpr Rect united = Union(Rect{Vector2{0.0F, 0.0F}, Vector2{10.0F, 10.0F}},
                                        Rect{Vector2{5.0F, 5.0F}, Vector2{10.0F, 10.0F}});
    EXPECT_EQ(united, (Rect{Vector2{0.0F, 0.0F}, Vector2{15.0F, 15.0F}}));
}

TEST(RectTest, Transform_ScalesExtentAndTranslatesPosition) {
    // UIRuntime's authored-to-presentation transform as one operation: position scales then
    // offsets, the extent scales but never translates.
    constexpr Rect rect{Vector2{10.0F, 20.0F}, Vector2{30.0F, 40.0F}};
    constexpr Rect transformed =
        Transform(rect, Vector2{2.0F, 2.0F}, Vector2{10.0F, 20.0F});
    EXPECT_EQ(transformed.position, (Vector2{30.0F, 60.0F}));
    EXPECT_EQ(transformed.size, (Vector2{60.0F, 80.0F}));
}

TEST(RectTest, Max_IsPositionPlusSize) {
    constexpr Rect rect{Vector2{10.0F, 20.0F}, Vector2{30.0F, 40.0F}};
    EXPECT_EQ(Max(rect), (Vector2{40.0F, 60.0F}));
}

TEST(RectTest, IsFinite_RejectsNonFiniteComponents) {
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    EXPECT_TRUE(IsFinite(Rect{Vector2{1.0F, 2.0F}, Vector2{3.0F, 4.0F}}));
    EXPECT_FALSE(IsFinite(Rect{Vector2{kNaN, 2.0F}, Vector2{3.0F, 4.0F}}));
    EXPECT_FALSE(IsFinite(Rect{Vector2{1.0F, 2.0F}, Vector2{3.0F, kInf}}));
}

TEST(RectTest, ToString_FormatsPositionAndSize) {
    EXPECT_EQ(ToString(Rect{Vector2{1.0F, 2.0F}, Vector2{3.0F, 4.0F}}),
              "((1.000000, 2.000000), (3.000000, 4.000000))");
}

} // namespace
} // namespace UVE::Math::Tests
