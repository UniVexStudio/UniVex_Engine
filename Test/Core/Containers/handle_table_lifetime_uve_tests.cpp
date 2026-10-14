// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/containers/handle_table_uve.h"
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
namespace {
using UVE::Containers::HandleTableUVE;
using UVE::Containers::SlotHandleUVE;
static_assert(noexcept(std::declval<HandleTableUVE<int> &>().ReleaseUVE({})));
static_assert(noexcept(std::declval<HandleTableUVE<int> &>().ClearUVE()));
struct TinyHandleUVE {
    std::uint32_t index = 0U, generation = 0U;
    static TinyHandleUVE FromIndexAndGenerationUVE(std::uint32_t i, std::uint32_t g) { return {i, g}; }
    std::uint32_t IndexUVE() const { return index; }
    std::uint32_t GenerationUVE() const { return generation; }
    static std::uint32_t NextGenerationUVE(std::uint32_t value) noexcept {
        return value == 3U ? 1U : value + 1U;
    }
};
TEST(HandleTableLifetimeUVETest, ClearAndReacquireNeverRevivesAnOldHandle) {
    HandleTableUVE<int> table;
    std::vector<SlotHandleUVE> history;
    for (int i = 0; i < 128; ++i) {
        const auto live = table.AcquireUVE(i);
        for (auto stale : history)
            EXPECT_EQ(table.FindUVE(stale), nullptr);
        history.push_back(live);
        table.ClearUVE();
        EXPECT_EQ(table.GetLiveCountUVE(), 0U);
    }
}
TEST(HandleTableLifetimeUVETest, ExhaustedGenerationRetiresInsteadOfWrapping) {
    HandleTableUVE<int, TinyHandleUVE> table;
    std::array<TinyHandleUVE, 3> old{};
    for (auto &handle : old) {
        handle = table.AcquireUVE(1);
        ASSERT_TRUE(table.ReleaseUVE(handle));
    }
    const auto live = table.AcquireUVE(42);
    EXPECT_NE(live.index, old.front().index);
    for (auto stale : old) {
        EXPECT_EQ(table.FindUVE(stale), nullptr);
        EXPECT_FALSE(table.ReleaseUVE(stale));
    }
}
TEST(HandleTableLifetimeUVETest, ClearRetiresExhaustedSlotsAndKeepsFreeGenerations) {
    HandleTableUVE<int, TinyHandleUVE> table;
    std::vector<TinyHandleUVE> old;
    for (int i = 0; i < 24; ++i) {
        const auto live = table.AcquireUVE(i);
        for (auto stale : old)
            EXPECT_EQ(table.FindUVE(stale), nullptr);
        old.push_back(live);
        table.ClearUVE();
        table.ClearUVE();
    }
    EXPECT_GT(old.back().index, old.front().index);
}
TEST(HandleTableLifetimeUVETest, MixedLiveAndReleasedSlotsStayStaleAfterClear) {
    HandleTableUVE<int> table;
    const auto a = table.AcquireUVE(1), b = table.AcquireUVE(2), c = table.AcquireUVE(3);
    ASSERT_TRUE(table.ReleaseUVE(b));
    table.ClearUVE();
    table.AcquireUVE(4);
    table.AcquireUVE(5);
    table.AcquireUVE(6);
    EXPECT_FALSE(table.ContainsUVE(a));
    EXPECT_FALSE(table.ContainsUVE(b));
    EXPECT_FALSE(table.ContainsUVE(c));
    EXPECT_EQ(table.GetLiveCountUVE(), 3U);
}
TEST(HandleTableLifetimeUVETest, FailedConstructorDoesNotLoseAFreeSlot) {
    struct ValueUVE {
        explicit ValueUVE(bool fail) {
            if (fail)
                throw std::runtime_error("construction");
        }
    };
    HandleTableUVE<ValueUVE> table;
    EXPECT_THROW(table.AcquireUVE(true), std::runtime_error);
    EXPECT_EQ(table.GetLiveCountUVE(), 0U);
    const auto first = table.AcquireUVE(false);
    ASSERT_TRUE(table.ReleaseUVE(first));
    EXPECT_THROW(table.AcquireUVE(true), std::runtime_error);
    const auto second = table.AcquireUVE(false);
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    EXPECT_EQ(table.GetLiveCountUVE(), 1U);
}
TEST(HandleTableLifetimeUVETest, ReleaseWithinIterationSupportsMoveOnlyOccupants) {
    HandleTableUVE<std::unique_ptr<int>> table;
    for (int i = 0; i < 16; ++i)
        table.AcquireUVE(std::make_unique<int>(i));
    int visited = 0;
    table.ForEachUVE([&](auto handle, const auto &value) {
        EXPECT_NE(value, nullptr);
        ++visited;
        EXPECT_TRUE(table.ReleaseUVE(handle));
    });
    EXPECT_EQ(visited, 16);
    EXPECT_EQ(table.GetLiveCountUVE(), 0U);
    for (int i = 0; i < 16; ++i)
        table.AcquireUVE(std::make_unique<int>(i));
    EXPECT_EQ(table.GetLiveCountUVE(), 16U);
}
TEST(HandleTableLifetimeUVETest, MoveTransfersFreeListAndResetsSourceBookkeeping) {
    HandleTableUVE<int> source;
    const auto a = source.AcquireUVE(1), b = source.AcquireUVE(2);
    ASSERT_TRUE(source.ReleaseUVE(a));
    HandleTableUVE<int> destination{std::move(source)};
    EXPECT_EQ(source.GetLiveCountUVE(), 0U);
    EXPECT_EQ(*destination.FindUVE(b), 2);
    const auto reused = destination.AcquireUVE(3);
    EXPECT_EQ(reused.index, a.index);
    EXPECT_NE(reused.generation, a.generation);
    const auto fresh = source.AcquireUVE(4);
    EXPECT_EQ(*source.FindUVE(fresh), 4);
    source = std::move(destination);
    EXPECT_EQ(destination.GetLiveCountUVE(), 0U);
    EXPECT_EQ(*source.FindUVE(reused), 3);
}
TEST(HandleTableLifetimeUVETest, CopyHasIndependentValuesAndFreeList) {
    HandleTableUVE<int> source;
    const auto free = source.AcquireUVE(1), live = source.AcquireUVE(2);
    ASSERT_TRUE(source.ReleaseUVE(free));
    auto copy = source;
    const auto copied = copy.AcquireUVE(3);
    EXPECT_EQ(copied.index, free.index);
    EXPECT_EQ(source.FindUVE(copied), nullptr);
    EXPECT_TRUE(copy.ReleaseUVE(live));
    EXPECT_TRUE(source.ContainsUVE(live));
}
} // namespace
