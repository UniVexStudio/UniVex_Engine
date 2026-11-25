// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "archetype_chunk.h"

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

struct UnregisteredComponent {
    int value = 0;
};

class ArchetypeChunkTest : public ::testing::Test {
protected:
    Memory::HeapAllocator allocator;
    std::unordered_map<std::type_index, ComponentTypeInfo> typeInfos{
        {std::type_index(typeid(TestComponent)), MakeComponentTypeInfo<TestComponent>()}};
    ArchetypeChunk chunk{allocator, {std::type_index(typeid(TestComponent))}, typeInfos};
};

TEST_F(ArchetypeChunkTest, ReserveRow_ReturnsSequentialRowsUntilFull) {
    for (std::size_t expectedRow = 0; expectedRow < kChunkCapacity; ++expectedRow) {
        EXPECT_EQ(chunk.ReserveRow(Entity{}), expectedRow);
    }
    EXPECT_TRUE(chunk.IsFull());
}

#if UVE_DEBUG
TEST_F(ArchetypeChunkTest, GetComponentPointer_UnregisteredComponentTypeAsserts) {
    static_cast<void>(chunk.ReserveRow(Entity{}));
    EXPECT_DEATH(
        { static_cast<void>(chunk.GetComponentPointer(std::type_index(typeid(UnregisteredComponent)), 0)); },
        "");
}
#else
TEST_F(ArchetypeChunkTest, GetComponentPointer_UnregisteredComponentTypeThrowsOutOfRange) {
    static_cast<void>(chunk.ReserveRow(Entity{}));
    EXPECT_THROW(
        { static_cast<void>(chunk.GetComponentPointer(std::type_index(typeid(UnregisteredComponent)), 0)); },
        std::out_of_range);
}
#endif

#if UVE_DEBUG
TEST_F(ArchetypeChunkTest, ReserveRow_OnFullChunkAsserts) {
    for (std::size_t i = 0; i < kChunkCapacity; ++i) {
        static_cast<void>(chunk.ReserveRow(Entity{}));
    }
    EXPECT_DEATH({ static_cast<void>(chunk.ReserveRow(Entity{})); }, "");
}

TEST_F(ArchetypeChunkTest, VacateRow_OutOfRangeRowAsserts) {
    EXPECT_DEATH({ static_cast<void>(chunk.VacateRow(0)); }, "");
}

TEST_F(ArchetypeChunkTest, GetEntityAtRow_OutOfRangeRowAsserts) {
    EXPECT_DEATH({ static_cast<void>(chunk.GetEntityAtRow(0)); }, "");
}
#else
TEST_F(ArchetypeChunkTest, ReserveRow_OnFullChunkReturnsSentinelInsteadOfOverrunningStorage) {
    for (std::size_t i = 0; i < kChunkCapacity; ++i) {
        static_cast<void>(chunk.ReserveRow(Entity{}));
    }
    EXPECT_EQ(chunk.ReserveRow(Entity{}), kInvalidRow);
    EXPECT_TRUE(chunk.IsFull()); // the failed reservation must not have grown m_count further
}

TEST_F(ArchetypeChunkTest, VacateRow_OutOfRangeRowReturnsInvalidEntityInsteadOfUnderflowing) {
    EXPECT_EQ(chunk.VacateRow(0), kInvalidEntity);
    EXPECT_EQ(chunk.GetCount(), 0U);
}

TEST_F(ArchetypeChunkTest, GetEntityAtRow_OutOfRangeRowReturnsInvalidEntity) {
    EXPECT_EQ(chunk.GetEntityAtRow(0), kInvalidEntity);
}
#endif

} // namespace
} // namespace UVE::Scene::Detail::Tests
