// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <utility>

#include <gtest/gtest.h>

#include "uve/memory/intrusive_ptr_uve.h"

namespace UVE::Memory::Tests {
namespace {

struct TrackedUVE final : public RefCountedUVE {
    explicit TrackedUVE(int* counterOut) : counter(counterOut) {}
    ~TrackedUVE() override { ++(*counter); }
    int value = 0;
    int* counter;
};

TEST(IntrusivePtrUVETest, FirstWrap_BringsAFreshObjectToOne) {
    int destructions = 0;
    IntrusivePtrUVE<TrackedUVE> owned{new TrackedUVE{&destructions}};
    EXPECT_EQ(owned->RefCountUVE(), 1U);
    owned->value = 7;
    EXPECT_EQ((*owned).value, 7);
}

TEST(IntrusivePtrUVETest, Copies_ShareAndLastReleaseDestroys) {
    int destructions = 0;
    IntrusivePtrUVE<TrackedUVE> first{new TrackedUVE{&destructions}};
    {
        IntrusivePtrUVE<TrackedUVE> second = first;
        IntrusivePtrUVE<TrackedUVE> third;
        third = second;
        EXPECT_EQ(first->RefCountUVE(), 3U);
        EXPECT_TRUE(first == second);
        EXPECT_FALSE(first != third);
    }
    EXPECT_EQ(first->RefCountUVE(), 1U);
    EXPECT_EQ(destructions, 0);
    first.ResetUVE();
    EXPECT_EQ(destructions, 1);
}

TEST(IntrusivePtrUVETest, Move_StealsWithoutTouchingTheCount) {
    int destructions = 0;
    IntrusivePtrUVE<TrackedUVE> moved{new TrackedUVE{&destructions}};
    IntrusivePtrUVE<TrackedUVE> stolen = std::move(moved);
    EXPECT_FALSE(static_cast<bool>(moved));
    EXPECT_EQ(stolen->RefCountUVE(), 1U);
    EXPECT_EQ(destructions, 0);
}

TEST(IntrusivePtrUVETest, Null_StaysEmptyAndComparesByIdentity) {
    const IntrusivePtrUVE<TrackedUVE> empty;
    EXPECT_FALSE(static_cast<bool>(empty));
    int destructions = 0;
    const IntrusivePtrUVE<TrackedUVE> other{new TrackedUVE{&destructions}};
    EXPECT_NE(empty, other);
}

} // namespace
} // namespace UVE::Memory::Tests
