// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "handle_table.h"
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
namespace {
using UVE::Containers::HandleTable;
using UVE::Containers::SlotHandle;
static_assert(noexcept(std::declval<HandleTable<int> &>().Release({})));
static_assert(noexcept(std::declval<HandleTable<int> &>().Clear()));
struct TinyHandle {
    std::uint32_t index = 0U, generation = 0U;
    static TinyHandle FromIndexAndGeneration(std::uint32_t i, std::uint32_t g) { return {i, g}; }
    std::uint32_t Index() const { return index; }
    std::uint32_t Generation() const { return generation; }
    static std::uint32_t NextGeneration(std::uint32_t value) noexcept {
        return value == 3U ? 1U : value + 1U;
    }
};
TEST(HandleTableLifetimeTest, ClearAndReacquireNeverRevivesAnOldHandle) {
    HandleTable<int> table;
    std::vector<SlotHandle> history;
    for (int i = 0; i < 128; ++i) {
        const auto live = table.Acquire(i);
        for (auto stale : history)
            EXPECT_EQ(table.Find(stale), nullptr);
        history.push_back(live);
        table.Clear();
        EXPECT_EQ(table.GetLiveCount(), 0U);
    }
}
TEST(HandleTableLifetimeTest, ExhaustedGenerationRetiresInsteadOfWrapping) {
    HandleTable<int, TinyHandle> table;
    std::array<TinyHandle, 3> old{};
    for (auto &handle : old) {
        handle = table.Acquire(1);
        ASSERT_TRUE(table.Release(handle));
    }
    const auto live = table.Acquire(42);
    EXPECT_NE(live.index, old.front().index);
    for (auto stale : old) {
        EXPECT_EQ(table.Find(stale), nullptr);
        EXPECT_FALSE(table.Release(stale));
    }
}
TEST(HandleTableLifetimeTest, ClearRetiresExhaustedSlotsAndKeepsFreeGenerations) {
    HandleTable<int, TinyHandle> table;
    std::vector<TinyHandle> old;
    for (int i = 0; i < 24; ++i) {
        const auto live = table.Acquire(i);
        for (auto stale : old)
            EXPECT_EQ(table.Find(stale), nullptr);
        old.push_back(live);
        table.Clear();
        table.Clear();
    }
    EXPECT_GT(old.back().index, old.front().index);
}
TEST(HandleTableLifetimeTest, MixedLiveAndReleasedSlotsStayStaleAfterClear) {
    HandleTable<int> table;
    const auto a = table.Acquire(1), b = table.Acquire(2), c = table.Acquire(3);
    ASSERT_TRUE(table.Release(b));
    table.Clear();
    table.Acquire(4);
    table.Acquire(5);
    table.Acquire(6);
    EXPECT_FALSE(table.Contains(a));
    EXPECT_FALSE(table.Contains(b));
    EXPECT_FALSE(table.Contains(c));
    EXPECT_EQ(table.GetLiveCount(), 3U);
}
TEST(HandleTableLifetimeTest, FailedConstructorDoesNotLoseAFreeSlot) {
    struct Value {
        explicit Value(bool fail) {
            if (fail)
                throw std::runtime_error("construction");
        }
    };
    HandleTable<Value> table;
    EXPECT_THROW(table.Acquire(true), std::runtime_error);
    EXPECT_EQ(table.GetLiveCount(), 0U);
    const auto first = table.Acquire(false);
    ASSERT_TRUE(table.Release(first));
    EXPECT_THROW(table.Acquire(true), std::runtime_error);
    const auto second = table.Acquire(false);
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    EXPECT_EQ(table.GetLiveCount(), 1U);
}
TEST(HandleTableLifetimeTest, ReleaseWithinIterationSupportsMoveOnlyOccupants) {
    HandleTable<std::unique_ptr<int>> table;
    for (int i = 0; i < 16; ++i)
        table.Acquire(std::make_unique<int>(i));
    int visited = 0;
    table.ForEach([&](auto handle, const auto &value) {
        EXPECT_NE(value, nullptr);
        ++visited;
        EXPECT_TRUE(table.Release(handle));
    });
    EXPECT_EQ(visited, 16);
    EXPECT_EQ(table.GetLiveCount(), 0U);
    for (int i = 0; i < 16; ++i)
        table.Acquire(std::make_unique<int>(i));
    EXPECT_EQ(table.GetLiveCount(), 16U);
}
TEST(HandleTableLifetimeTest, MoveTransfersFreeListAndResetsSourceBookkeeping) {
    HandleTable<int> source;
    const auto a = source.Acquire(1), b = source.Acquire(2);
    ASSERT_TRUE(source.Release(a));
    HandleTable<int> destination{std::move(source)};
    EXPECT_EQ(source.GetLiveCount(), 0U);
    EXPECT_EQ(*destination.Find(b), 2);
    const auto reused = destination.Acquire(3);
    EXPECT_EQ(reused.index, a.index);
    EXPECT_NE(reused.generation, a.generation);
    const auto fresh = source.Acquire(4);
    EXPECT_EQ(*source.Find(fresh), 4);
    source = std::move(destination);
    EXPECT_EQ(destination.GetLiveCount(), 0U);
    EXPECT_EQ(*source.Find(reused), 3);
}
TEST(HandleTableLifetimeTest, CopyHasIndependentValuesAndFreeList) {
    HandleTable<int> source;
    const auto free = source.Acquire(1), live = source.Acquire(2);
    ASSERT_TRUE(source.Release(free));
    auto copy = source;
    const auto copied = copy.Acquire(3);
    EXPECT_EQ(copied.index, free.index);
    EXPECT_EQ(source.Find(copied), nullptr);
    EXPECT_TRUE(copy.Release(live));
    EXPECT_TRUE(source.Contains(live));
}
} // namespace
