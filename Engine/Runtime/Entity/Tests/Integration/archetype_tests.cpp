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
