// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "archetype.h"

#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "heap_allocator.h"
#include "platform.h"
#include "component_type_info.h"
#include "entity.h"

namespace UVE::Scene::Detail::Tests {
namespace {

struct TestComponent {
    int value = 0;
};

class ArchetypeTest : public ::testing::Test {
protected:
    Memory::HeapAllocator allocator;
    std::unordered_map<std::type_index, ComponentTypeInfo> typeInfos{
        {std::type_index(typeid(TestComponent)), MakeComponentTypeInfo<TestComponent>()}};
    ArchetypeSignature signature{std::vector<std::type_index>{std::type_index(typeid(TestComponent))}};
    Archetype archetype{signature, allocator, typeInfos};
};

TEST_F(ArchetypeTest, ReserveEntity_GrowsCountAndReturnsLocations) {
    const Archetype::Location location = archetype.ReserveEntity(Entity{});
    EXPECT_EQ(location.chunkIndex, 0U);
    EXPECT_EQ(location.row, 0U);
    EXPECT_EQ(archetype.GetEntityCount(), 1U);
}

TEST_F(ArchetypeTest, ReserveAndDestroy_ReusesVacatedChunksAndTrimsTrailingEmptyChunks) {
    std::vector<Archetype::Location> firstWave;
    firstWave.reserve(kChunkCapacity + 4U);
    for (std::size_t i = 0U; i < kChunkCapacity + 4U; ++i) {
        Entity e;
        e.index = static_cast<std::uint32_t>(i + 1U);
        firstWave.push_back(archetype.ReserveEntity(e));
    }
    ASSERT_EQ(firstWave.back().chunkIndex, 1U);

    // Vacate rows in chunk 0; subsequent reservations must reuse chunk 0 instead of allocating chunk 2.
    static_cast<void>(archetype.DestroyEntityAt(Archetype::Location{0U, 0U}));
    const Archetype::Location reused = archetype.ReserveEntity(Entity{9999U, 0U});
    EXPECT_EQ(reused.chunkIndex, 0U);

    // Empty the trailing chunk; live chunk count drops to 1 and further churn stays within chunk 0.
    for (std::size_t i = 0U; i < 4U; ++i) {
        static_cast<void>(archetype.DestroyEntityAt(Archetype::Location{1U, 0U}));
    }
    std::size_t occupiedChunks = 0U;
    archetype.ForEachChunk([&occupiedChunks](ArchetypeChunk&, std::size_t) { ++occupiedChunks; });
    EXPECT_EQ(occupiedChunks, 1U);
    EXPECT_EQ(archetype.GetEntityCount(), kChunkCapacity);
}

#if UVE_DEBUG
TEST_F(ArchetypeTest, DestroyEntityAt_OutOfRangeChunkIndexAsserts) {
    EXPECT_DEATH({ static_cast<void>(archetype.DestroyEntityAt(Archetype::Location{5, 0})); }, "");
}

TEST_F(ArchetypeTest, VacateWithoutDestroy_OutOfRangeChunkIndexAsserts) {
    EXPECT_DEATH({ static_cast<void>(archetype.VacateWithoutDestroy(Archetype::Location{5, 0})); }, "");
}

TEST_F(ArchetypeTest, GetChunk_OutOfRangeChunkIndexAsserts) {
    EXPECT_DEATH({ static_cast<void>(archetype.GetChunk(5)); }, "");
}
#else
TEST_F(ArchetypeTest, DestroyEntityAt_OutOfRangeChunkIndexReturnsInvalidEntityInsteadOfIndexingPastChunks) {
    EXPECT_EQ(archetype.DestroyEntityAt(Archetype::Location{5, 0}), kInvalidEntity);
}

TEST_F(ArchetypeTest, VacateWithoutDestroy_OutOfRangeChunkIndexReturnsInvalidEntity) {
    EXPECT_EQ(archetype.VacateWithoutDestroy(Archetype::Location{5, 0}), kInvalidEntity);
}

TEST_F(ArchetypeTest, GetChunk_OutOfRangeChunkIndexThrowsOutOfRangeInsteadOfIndexingPastChunks) {
    EXPECT_THROW({ static_cast<void>(archetype.GetChunk(5)); }, std::out_of_range);

    const Archetype& constArchetype = archetype;
    EXPECT_THROW({ static_cast<void>(constArchetype.GetChunk(5)); }, std::out_of_range);
}
#endif

} // namespace
} // namespace UVE::Scene::Detail::Tests
