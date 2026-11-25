// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "handle_table.h"
#include "engine_assert.h"

namespace {

using UVE::Containers::HandleTable;
using UVE::Containers::SlotHandle;

// A VoiceHandle-shaped packed handle (16-bit index + 16-bit generation in one uint32_t,
// value 0 invalid): proves the table's handle concept works for packed layouts without this
// test depending on the Audio module.
struct PackedHandle {
    std::uint32_t value = 0U;

    [[nodiscard]] static PackedHandle FromIndexAndGeneration(std::uint32_t index,
                                                                  std::uint32_t generation) {
        UVE_ASSERT(index <= 0xFFFFU);
        UVE_ASSERT(generation <= 0xFFFFU);
        return PackedHandle{(generation << 16U) | index};
    }

    [[nodiscard]] constexpr std::uint32_t Index() const noexcept { return value & 0xFFFFU; }
    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept { return value >> 16U; }

    [[nodiscard]] static constexpr std::uint32_t NextGeneration(std::uint32_t current) noexcept {
        const std::uint32_t next = (current + 1U) & 0xFFFFU;
        return next == 0U ? 1U : next;
    }
};

[[nodiscard]] constexpr bool operator==(const PackedHandle& lhs,
                                        const PackedHandle& rhs) noexcept {
    return lhs.value == rhs.value;
}

struct NoDefault {
    int id = 0;
    std::string name;

    NoDefault(int idIn, std::string nameIn) : id(idIn), name(std::move(nameIn)) {}
};

struct MoveOnly {
    std::unique_ptr<int> owned;

    explicit MoveOnly(int value) : owned(std::make_unique<int>(value)) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;
};

TEST(HandleTableTest, Acquire_ReturnsDistinctValidHandles) {
    HandleTable<std::string> table;
    const SlotHandle first = table.Acquire("a");
    const SlotHandle second = table.Acquire("b");
    const SlotHandle third = table.Acquire("c");

    EXPECT_NE(first, SlotHandle::Invalid());
    EXPECT_NE(first, second);
    EXPECT_NE(first, third);
    EXPECT_NE(second, third);
    EXPECT_EQ(table.GetLiveCount(), 3U);
    EXPECT_EQ(*table.Find(first), "a");
    EXPECT_EQ(*table.Find(third), "c");
    EXPECT_TRUE(table.Contains(second));
}

TEST(HandleTableTest, Release_InvalidatesOnlyThatHandle) {
    HandleTable<int> table;
    const SlotHandle first = table.Acquire(1);
    const SlotHandle second = table.Acquire(2);
    const SlotHandle third = table.Acquire(3);

    EXPECT_TRUE(table.Release(second));
    EXPECT_EQ(table.Find(second), nullptr);
    EXPECT_FALSE(table.Contains(second));
    EXPECT_EQ(table.GetLiveCount(), 2U);
    EXPECT_EQ(*table.Find(first), 1);
    EXPECT_EQ(*table.Find(third), 3);
}

TEST(HandleTableTest, SlotReuse_BumpsGenerationSoStaleHandlesStayDead) {
    HandleTable<int> table;
    const SlotHandle first = table.Acquire(10);
    ASSERT_TRUE(table.Release(first));

    const SlotHandle second = table.Acquire(20);
    // The freed slot is reused (same index) but the handle differs (bumped generation), so the
    // stale first handle cannot alias the new occupant.
    EXPECT_EQ(second.Index(), first.Index());
    EXPECT_NE(second.Generation(), first.Generation());
    EXPECT_NE(second, first);
    EXPECT_EQ(table.Find(first), nullptr);
    EXPECT_FALSE(table.Contains(first));
    ASSERT_NE(table.Find(second), nullptr);
    EXPECT_EQ(*table.Find(second), 20);
    EXPECT_EQ(table.GetLiveCount(), 1U);
}

TEST(HandleTableTest, InvalidAndForeignHandles_AreSafeEverywhere) {
    HandleTable<int> table;
    EXPECT_EQ(table.Find(SlotHandle::Invalid()), nullptr);
    EXPECT_FALSE(table.Contains(SlotHandle::Invalid()));
    EXPECT_FALSE(table.Release(SlotHandle::Invalid()));

    const SlotHandle live = table.Acquire(7);
    EXPECT_EQ(table.Find(SlotHandle::Invalid()), nullptr);
    EXPECT_FALSE(table.Release(SlotHandle::Invalid()));
    // A well-formed but never-issued handle: in-range index, wrong generation, then out of range.
    EXPECT_FALSE(table.Contains(SlotHandle::FromIndexAndGeneration(0U, 999U)));
    EXPECT_FALSE(table.Release(SlotHandle::FromIndexAndGeneration(50U, 1U)));
    EXPECT_EQ(table.GetLiveCount(), 1U);
    EXPECT_EQ(*table.Find(live), 7);
}

TEST(HandleTableTest, DoubleRelease_SecondReturnsFalse) {
    HandleTable<int> table;
    const SlotHandle handle = table.Acquire(1);
    EXPECT_TRUE(table.Release(handle));
    EXPECT_FALSE(table.Release(handle));
    EXPECT_EQ(table.GetLiveCount(), 0U);
}

TEST(HandleTableTest, EmplaceConstructs_InPlaceWithoutRequiringDefaultConstruction) {
    HandleTable<NoDefault> table;
    const SlotHandle handle = table.Acquire(42, std::string("forwarded"));

    const NoDefault* const found = table.Find(handle);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 42);
    EXPECT_EQ(found->name, "forwarded");
}

TEST(HandleTableTest, MoveOnlyType_SupportedThroughEmplacement) {
    HandleTable<MoveOnly> table;
    MoveOnly value(9);
    const SlotHandle handle = table.Acquire(std::move(value));

    const MoveOnly* const found = table.Find(handle);
    ASSERT_NE(found, nullptr);
    ASSERT_NE(found->owned, nullptr);
    EXPECT_EQ(*found->owned, 9);
    EXPECT_TRUE(table.Release(handle));
}

TEST(HandleTableTest, ForEach_VisitsLiveEntriesOnlyInSlotOrder) {
    HandleTable<int> table;
    const SlotHandle first = table.Acquire(1);
    const SlotHandle second = table.Acquire(2);
    table.Acquire(3);
    ASSERT_TRUE(table.Release(second));

    std::vector<std::pair<SlotHandle, int>> visited;
    table.ForEach([&](SlotHandle handle, int& value) { visited.emplace_back(handle, value); });
    ASSERT_EQ(visited.size(), 2U);
    EXPECT_EQ(visited[0].first, first);
    EXPECT_EQ(visited[0].second, 1);
    EXPECT_EQ(visited[1].second, 3);

    const HandleTable<int>& constTable = table;
    std::size_t constVisited = 0U;
    constTable.ForEach(
        [&](SlotHandle /*handle*/, const int& /*value*/) { ++constVisited; });
    EXPECT_EQ(constVisited, 2U);
}

TEST(HandleTableTest, Clear_RemovesAllAndInvalidatesOldHandles) {
    HandleTable<int> table;
    const SlotHandle first = table.Acquire(1);
    table.Acquire(2);
    table.Clear();

    EXPECT_EQ(table.GetLiveCount(), 0U);
    EXPECT_EQ(table.Find(first), nullptr);
    EXPECT_FALSE(table.Contains(first));

    // The cleared table is reusable.
    const SlotHandle reused = table.Acquire(3);
    EXPECT_EQ(table.GetLiveCount(), 1U);
    EXPECT_EQ(*table.Find(reused), 3);
}

TEST(HandleTableTest, PackedCustomHandle_FullCycleThroughTheConcept) {
    HandleTable<std::string, PackedHandle> table;
    const PackedHandle first = table.Acquire("voice");
    EXPECT_NE(first.value, 0U);
    EXPECT_EQ(*table.Find(first), "voice");

    ASSERT_TRUE(table.Release(first));
    EXPECT_EQ(table.Find(first), nullptr);

    const PackedHandle second = table.Acquire("reused");
    EXPECT_EQ(second.Index(), first.Index());
    EXPECT_NE(second.value, first.value);
    EXPECT_EQ(*table.Find(second), "reused");

    // Packed generation wraps within its own width and still skips the invalid zero.
    EXPECT_EQ(PackedHandle::NextGeneration(0U), 1U);
    EXPECT_EQ(PackedHandle::NextGeneration(0xFFFFU), 1U);
}

TEST(HandleTableTest, SlotHandle_NextGenerationSkipsZero) {
    EXPECT_EQ(SlotHandle::NextGeneration(0U), 1U);
    EXPECT_EQ(SlotHandle::NextGeneration(41U), 42U);
    EXPECT_EQ(SlotHandle::NextGeneration(0xFFFFFFFFU), 1U);
    EXPECT_EQ(SlotHandle::Invalid(), SlotHandle{});
}

}  // namespace
