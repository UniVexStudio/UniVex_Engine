// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <utility>

#include <gtest/gtest.h>

#include "intrusive_ptr.h"

namespace UVE::Memory::Tests {
namespace {

struct Tracked final : public RefCounted {
    explicit Tracked(int* counterOut) : counter(counterOut) {}
    ~Tracked() override { ++(*counter); }
    int value = 0;
    int* counter;
};

TEST(IntrusivePtrTest, FirstWrap_BringsAFreshObjectToOne) {
    int destructions = 0;
    IntrusivePtr<Tracked> owned{new Tracked{&destructions}};
    EXPECT_EQ(owned->RefCount(), 1U);
    owned->value = 7;
    EXPECT_EQ((*owned).value, 7);
}

TEST(IntrusivePtrTest, Copies_ShareAndLastReleaseDestroys) {
    int destructions = 0;
    IntrusivePtr<Tracked> first{new Tracked{&destructions}};
    {
        IntrusivePtr<Tracked> second = first;
        IntrusivePtr<Tracked> third;
        third = second;
        EXPECT_EQ(first->RefCount(), 3U);
        EXPECT_TRUE(first == second);
        EXPECT_FALSE(first != third);
    }
    EXPECT_EQ(first->RefCount(), 1U);
    EXPECT_EQ(destructions, 0);
    first.Reset();
    EXPECT_EQ(destructions, 1);
}

TEST(IntrusivePtrTest, Move_StealsWithoutTouchingTheCount) {
    int destructions = 0;
    IntrusivePtr<Tracked> moved{new Tracked{&destructions}};
    IntrusivePtr<Tracked> stolen = std::move(moved);
    EXPECT_FALSE(static_cast<bool>(moved));
    EXPECT_EQ(stolen->RefCount(), 1U);
    EXPECT_EQ(destructions, 0);
}

TEST(IntrusivePtrTest, Null_StaysEmptyAndComparesByIdentity) {
    const IntrusivePtr<Tracked> empty;
    EXPECT_FALSE(static_cast<bool>(empty));
    int destructions = 0;
    const IntrusivePtr<Tracked> other{new Tracked{&destructions}};
    EXPECT_NE(empty, other);
}

} // namespace
} // namespace UVE::Memory::Tests
