// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/containers/handle_table_uve.h"
#include "uve/logging/assert_uve.h"

namespace {

using UVE::Containers::HandleTableUVE;
using UVE::Containers::SlotHandleUVE;

// A VoiceHandleUVE-shaped packed handle (16-bit index + 16-bit generation in one uint32_t,
// value 0 invalid): proves the table's handle concept works for packed layouts without this
// test depending on the Audio module.
struct PackedHandleUVE {
    std::uint32_t value = 0U;

    [[nodiscard]] static PackedHandleUVE FromIndexAndGenerationUVE(std::uint32_t index,
                                                                  std::uint32_t generation) {
        UVE_ASSERT(index <= 0xFFFFU);
        UVE_ASSERT(generation <= 0xFFFFU);
        return PackedHandleUVE{(generation << 16U) | index};
    }

    [[nodiscard]] constexpr std::uint32_t IndexUVE() const noexcept { return value & 0xFFFFU; }
    [[nodiscard]] constexpr std::uint32_t GenerationUVE() const noexcept { return value >> 16U; }

    [[nodiscard]] static constexpr std::uint32_t NextGenerationUVE(std::uint32_t current) noexcept {
        const std::uint32_t next = (current + 1U) & 0xFFFFU;
        return next == 0U ? 1U : next;
    }
};

[[nodiscard]] constexpr bool operator==(const PackedHandleUVE& lhs,
                                        const PackedHandleUVE& rhs) noexcept {
    return lhs.value == rhs.value;
}

struct NoDefaultUVE {
    int id = 0;
    std::string name;

    NoDefaultUVE(int idIn, std::string nameIn) : id(idIn), name(std::move(nameIn)) {}
};

struct MoveOnlyUVE {
    std::unique_ptr<int> owned;

    explicit MoveOnlyUVE(int value) : owned(std::make_unique<int>(value)) {}
    MoveOnlyUVE(const MoveOnlyUVE&) = delete;
    MoveOnlyUVE& operator=(const MoveOnlyUVE&) = delete;
    MoveOnlyUVE(MoveOnlyUVE&&) noexcept = default;
    MoveOnlyUVE& operator=(MoveOnlyUVE&&) noexcept = default;
};

TEST(HandleTableUVETest, Acquire_ReturnsDistinctValidHandles) {
    HandleTableUVE<std::string> table;
    const SlotHandleUVE first = table.AcquireUVE("a");
    const SlotHandleUVE second = table.AcquireUVE("b");
    const SlotHandleUVE third = table.AcquireUVE("c");

    EXPECT_NE(first, SlotHandleUVE::InvalidUVE());
    EXPECT_NE(first, second);
    EXPECT_NE(first, third);
    EXPECT_NE(second, third);
    EXPECT_EQ(table.GetLiveCountUVE(), 3U);
    EXPECT_EQ(*table.FindUVE(first), "a");
    EXPECT_EQ(*table.FindUVE(third), "c");
    EXPECT_TRUE(table.ContainsUVE(second));
}

TEST(HandleTableUVETest, Release_InvalidatesOnlyThatHandle) {
    HandleTableUVE<int> table;
    const SlotHandleUVE first = table.AcquireUVE(1);
    const SlotHandleUVE second = table.AcquireUVE(2);
    const SlotHandleUVE third = table.AcquireUVE(3);

    EXPECT_TRUE(table.ReleaseUVE(second));
    EXPECT_EQ(table.FindUVE(second), nullptr);
    EXPECT_FALSE(table.ContainsUVE(second));
    EXPECT_EQ(table.GetLiveCountUVE(), 2U);
    EXPECT_EQ(*table.FindUVE(first), 1);
    EXPECT_EQ(*table.FindUVE(third), 3);
}

TEST(HandleTableUVETest, SlotReuse_BumpsGenerationSoStaleHandlesStayDead) {
    HandleTableUVE<int> table;
    const SlotHandleUVE first = table.AcquireUVE(10);
    ASSERT_TRUE(table.ReleaseUVE(first));

    const SlotHandleUVE second = table.AcquireUVE(20);
    // The freed slot is reused (same index) but the handle differs (bumped generation), so the
    // stale first handle cannot alias the new occupant.
    EXPECT_EQ(second.IndexUVE(), first.IndexUVE());
    EXPECT_NE(second.GenerationUVE(), first.GenerationUVE());
    EXPECT_NE(second, first);
    EXPECT_EQ(table.FindUVE(first), nullptr);
    EXPECT_FALSE(table.ContainsUVE(first));
    ASSERT_NE(table.FindUVE(second), nullptr);
    EXPECT_EQ(*table.FindUVE(second), 20);
    EXPECT_EQ(table.GetLiveCountUVE(), 1U);
}

TEST(HandleTableUVETest, InvalidAndForeignHandles_AreSafeEverywhere) {
    HandleTableUVE<int> table;
    EXPECT_EQ(table.FindUVE(SlotHandleUVE::InvalidUVE()), nullptr);
    EXPECT_FALSE(table.ContainsUVE(SlotHandleUVE::InvalidUVE()));
    EXPECT_FALSE(table.ReleaseUVE(SlotHandleUVE::InvalidUVE()));

    const SlotHandleUVE live = table.AcquireUVE(7);
    EXPECT_EQ(table.FindUVE(SlotHandleUVE::InvalidUVE()), nullptr);
    EXPECT_FALSE(table.ReleaseUVE(SlotHandleUVE::InvalidUVE()));
    // A well-formed but never-issued handle: in-range index, wrong generation, then out of range.
    EXPECT_FALSE(table.ContainsUVE(SlotHandleUVE::FromIndexAndGenerationUVE(0U, 999U)));
    EXPECT_FALSE(table.ReleaseUVE(SlotHandleUVE::FromIndexAndGenerationUVE(50U, 1U)));
    EXPECT_EQ(table.GetLiveCountUVE(), 1U);
    EXPECT_EQ(*table.FindUVE(live), 7);
}

TEST(HandleTableUVETest, DoubleRelease_SecondReturnsFalse) {
    HandleTableUVE<int> table;
    const SlotHandleUVE handle = table.AcquireUVE(1);
    EXPECT_TRUE(table.ReleaseUVE(handle));
    EXPECT_FALSE(table.ReleaseUVE(handle));
    EXPECT_EQ(table.GetLiveCountUVE(), 0U);
}

TEST(HandleTableUVETest, EmplaceConstructs_InPlaceWithoutRequiringDefaultConstruction) {
    HandleTableUVE<NoDefaultUVE> table;
    const SlotHandleUVE handle = table.AcquireUVE(42, std::string("forwarded"));

    const NoDefaultUVE* const found = table.FindUVE(handle);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 42);
    EXPECT_EQ(found->name, "forwarded");
}

TEST(HandleTableUVETest, MoveOnlyType_SupportedThroughEmplacement) {
    HandleTableUVE<MoveOnlyUVE> table;
    MoveOnlyUVE value(9);
    const SlotHandleUVE handle = table.AcquireUVE(std::move(value));

    const MoveOnlyUVE* const found = table.FindUVE(handle);
    ASSERT_NE(found, nullptr);
    ASSERT_NE(found->owned, nullptr);
    EXPECT_EQ(*found->owned, 9);
    EXPECT_TRUE(table.ReleaseUVE(handle));
}

TEST(HandleTableUVETest, ForEach_VisitsLiveEntriesOnlyInSlotOrder) {
    HandleTableUVE<int> table;
    const SlotHandleUVE first = table.AcquireUVE(1);
    const SlotHandleUVE second = table.AcquireUVE(2);
    table.AcquireUVE(3);
    ASSERT_TRUE(table.ReleaseUVE(second));

    std::vector<std::pair<SlotHandleUVE, int>> visited;
    table.ForEachUVE([&](SlotHandleUVE handle, int& value) { visited.emplace_back(handle, value); });
    ASSERT_EQ(visited.size(), 2U);
    EXPECT_EQ(visited[0].first, first);
    EXPECT_EQ(visited[0].second, 1);
    EXPECT_EQ(visited[1].second, 3);

    const HandleTableUVE<int>& constTable = table;
    std::size_t constVisited = 0U;
    constTable.ForEachUVE(
        [&](SlotHandleUVE /*handle*/, const int& /*value*/) { ++constVisited; });
    EXPECT_EQ(constVisited, 2U);
}

TEST(HandleTableUVETest, Clear_RemovesAllAndInvalidatesOldHandles) {
    HandleTableUVE<int> table;
    const SlotHandleUVE first = table.AcquireUVE(1);
    table.AcquireUVE(2);
    table.ClearUVE();

    EXPECT_EQ(table.GetLiveCountUVE(), 0U);
    EXPECT_EQ(table.FindUVE(first), nullptr);
    EXPECT_FALSE(table.ContainsUVE(first));

    // The cleared table is reusable.
    const SlotHandleUVE reused = table.AcquireUVE(3);
    EXPECT_EQ(table.GetLiveCountUVE(), 1U);
    EXPECT_EQ(*table.FindUVE(reused), 3);
}

TEST(HandleTableUVETest, PackedCustomHandle_FullCycleThroughTheConcept) {
    HandleTableUVE<std::string, PackedHandleUVE> table;
    const PackedHandleUVE first = table.AcquireUVE("voice");
    EXPECT_NE(first.value, 0U);
    EXPECT_EQ(*table.FindUVE(first), "voice");

    ASSERT_TRUE(table.ReleaseUVE(first));
    EXPECT_EQ(table.FindUVE(first), nullptr);

    const PackedHandleUVE second = table.AcquireUVE("reused");
    EXPECT_EQ(second.IndexUVE(), first.IndexUVE());
    EXPECT_NE(second.value, first.value);
    EXPECT_EQ(*table.FindUVE(second), "reused");

    // Packed generation wraps within its own width and still skips the invalid zero.
    EXPECT_EQ(PackedHandleUVE::NextGenerationUVE(0U), 1U);
    EXPECT_EQ(PackedHandleUVE::NextGenerationUVE(0xFFFFU), 1U);
}

TEST(HandleTableUVETest, SlotHandle_NextGenerationSkipsZero) {
    EXPECT_EQ(SlotHandleUVE::NextGenerationUVE(0U), 1U);
    EXPECT_EQ(SlotHandleUVE::NextGenerationUVE(41U), 42U);
    EXPECT_EQ(SlotHandleUVE::NextGenerationUVE(0xFFFFFFFFU), 1U);
    EXPECT_EQ(SlotHandleUVE::InvalidUVE(), SlotHandleUVE{});
}

}  // namespace
