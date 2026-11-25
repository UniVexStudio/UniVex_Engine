// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "rect_int.h"

#include <cstdint>
#include <limits>
#include <string>

#include "rect.h"
#include "vector2i.h"

#include <gtest/gtest.h>

namespace UVE::Math::Tests {
namespace {

TEST(RectIntTest, DefaultConstruction_IsEmptyAtOrigin) {
    constexpr RectInt rect{};
    EXPECT_EQ(rect.position, (Vector2i{0, 0}));
    EXPECT_EQ(rect.size, (Vector2i{0, 0}));
}

TEST(RectIntTest, EqualityOperators_ComparePositionAndSize) {
    constexpr RectInt a{Vector2i{1, 2}, Vector2i{3, 4}};
    constexpr RectInt b{Vector2i{1, 2}, Vector2i{3, 4}};
    constexpr RectInt c{Vector2i{1, 2}, Vector2i{3, 5}};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(RectIntTest, ContainsPoint_EdgesAreIncluded) {
    constexpr RectInt rect{Vector2i{10, 20}, Vector2i{30, 40}};
    EXPECT_TRUE(Contains(rect, Vector2i{10, 20}));
    EXPECT_TRUE(Contains(rect, Vector2i{40, 60}));
    EXPECT_TRUE(Contains(rect, Vector2i{25, 60}));
    EXPECT_FALSE(Contains(rect, Vector2i{9, 45}));
    EXPECT_FALSE(Contains(rect, Vector2i{41, 45}));
}

TEST(RectIntTest, ContainsRect_ServesTheViewportFitCheck) {
    // The render-pass viewport override must fit its target: edges included, negatives rejected.
    constexpr RectInt target{Vector2i{0, 0}, Vector2i{64, 64}};
    EXPECT_TRUE(Contains(target, RectInt{Vector2i{0, 0}, Vector2i{32, 64}}));
    EXPECT_TRUE(Contains(target, RectInt{Vector2i{32, 0}, Vector2i{32, 64}}));
    EXPECT_TRUE(Contains(target, target));
    EXPECT_FALSE(Contains(target, RectInt{Vector2i{33, 0}, Vector2i{32, 64}}));
    EXPECT_FALSE(Contains(target, RectInt{Vector2i{-1, 0}, Vector2i{32, 64}}));
}

TEST(RectIntTest, ContainsPoint_NearInt32MaxDoesNotOverflow) {
    // position.x + size.x overflows int32 here; the widened comparison must still hold.
    constexpr std::int32_t kNearMax = std::numeric_limits<std::int32_t>::max() - 10;
    constexpr RectInt rect{Vector2i{kNearMax, 0}, Vector2i{20, 10}};
    EXPECT_TRUE(Contains(rect, Vector2i{std::numeric_limits<std::int32_t>::max() - 1, 5}));
    EXPECT_FALSE(Contains(rect, Vector2i{kNearMax - 1, 5}));
}

TEST(RectIntTest, Intersects_EdgeTouchingIsNotAnOverlap) {
    constexpr RectInt a{Vector2i{0, 0}, Vector2i{10, 10}};
    EXPECT_TRUE(Intersects(a, RectInt{Vector2i{5, 5}, Vector2i{10, 10}}));
    EXPECT_FALSE(Intersects(a, RectInt{Vector2i{10, 0}, Vector2i{10, 10}}));
    EXPECT_FALSE(Intersects(a, RectInt{Vector2i{20, 20}, Vector2i{5, 5}}));
}

TEST(RectIntTest, Intersection_ComputesOverlapAndEmptyForDisjoint) {
    constexpr RectInt overlap =
        Intersection(RectInt{Vector2i{0, 0}, Vector2i{10, 10}},
                        RectInt{Vector2i{5, 5}, Vector2i{10, 10}});
    EXPECT_EQ(overlap, (RectInt{Vector2i{5, 5}, Vector2i{5, 5}}));
    constexpr RectInt empty =
        Intersection(RectInt{Vector2i{0, 0}, Vector2i{10, 10}},
                        RectInt{Vector2i{20, 20}, Vector2i{5, 5}});
    EXPECT_EQ(empty.size, (Vector2i{0, 0}));
}

TEST(RectIntTest, Union_SpansBothRects) {
    constexpr RectInt united =
        Union(RectInt{Vector2i{0, 0}, Vector2i{10, 10}},
                 RectInt{Vector2i{5, 5}, Vector2i{10, 10}});
    EXPECT_EQ(united, (RectInt{Vector2i{0, 0}, Vector2i{15, 15}}));
}

TEST(RectIntTest, ToRect_ConvertsPositionAndSize) {
    constexpr RectInt rect{Vector2i{-7, 42}, Vector2i{64, 32}};
    const Rect f = ToRect(rect);
    EXPECT_EQ(f.position, (Vector2{-7.0F, 42.0F}));
    EXPECT_EQ(f.size, (Vector2{64.0F, 32.0F}));
}

TEST(RectIntTest, ToString_FormatsPositionAndSize) {
    EXPECT_EQ(ToString(RectInt{Vector2i{1, 2}, Vector2i{3, 4}}), "((1, 2), (3, 4))");
}

} // namespace
} // namespace UVE::Math::Tests
