// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/entity/entity_manager_uve.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/platform/platform_uve.h"
#include "uve/entity/entity_lifecycle_events_uve.h"

namespace UVE::Scene::Tests {
namespace {

struct PositionComponentUVE {
    int value = 0;
};

struct TagComponentUVE {
    int value = 0;
};

struct NonTrivialComponentUVE {
    std::string name;
};

class EntityManagerUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(EntityManagerUVETest, CreateEntityUVE_IsAliveAndCounted) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    EXPECT_TRUE(entityManager.IsAliveUVE(entity));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), 1U);
}

TEST_F(EntityManagerUVETest, DestroyEntityUVE_IsNoLongerAlive) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    EXPECT_FALSE(entityManager.IsAliveUVE(entity));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), 0U);
}

#if UVE_DEBUG
TEST_F(EntityManagerUVETest, DestroyEntityUVE_StaleHandleAsserts) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    EXPECT_DEATH({ entityManager.DestroyEntityUVE(entity); }, "");
}

TEST_F(EntityManagerUVETest, HasComponentUVE_StaleHandleAsserts) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    EXPECT_DEATH({ static_cast<void>(entityManager.HasComponentUVE<PositionComponentUVE>(entity)); }, "");
}

TEST_F(EntityManagerUVETest, GetComponentTypesUVE_StaleHandleAsserts) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    EXPECT_DEATH({ static_cast<void>(entityManager.GetComponentTypesUVE(entity)); }, "");
}
#else
TEST_F(EntityManagerUVETest, StaleHandleQueriesAndDestroyFailClosed) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);

    EXPECT_NO_FATAL_FAILURE(entityManager.DestroyEntityUVE(entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<PositionComponentUVE>(entity));
    EXPECT_TRUE(entityManager.GetComponentTypesUVE(entity).empty());
    EXPECT_EQ(entityManager.GetEntityCountUVE(), 0U);
}
#endif

TEST_F(EntityManagerUVETest, ReusedIndex_YieldsDifferentGeneration) {
    const EntityUVE first = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(first);
    const EntityUVE second = entityManager.CreateEntityUVE();

    EXPECT_EQ(first.index, second.index);
    EXPECT_NE(first.generation, second.generation);
    EXPECT_FALSE(entityManager.IsAliveUVE(first));
    EXPECT_TRUE(entityManager.IsAliveUVE(second));
}

#if UVE_DEBUG
TEST_F(EntityManagerUVETest, RemoveComponentUVE_StaleHandleAsserts) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    EXPECT_DEATH({ entityManager.RemoveComponentUVE<PositionComponentUVE>(entity); }, "");
}

TEST_F(EntityManagerUVETest, RemoveComponentUVE_AbsentComponentAsserts) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    EXPECT_DEATH({ entityManager.RemoveComponentUVE<PositionComponentUVE>(entity); }, "");
}
#else
TEST_F(EntityManagerUVETest, RemoveComponentUVE_InvalidRequestsFailClosed) {
    const EntityUVE stale = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(stale);
    EXPECT_NO_FATAL_FAILURE(entityManager.RemoveComponentUVE<PositionComponentUVE>(stale));

    const EntityUVE live = entityManager.CreateEntityUVE();
    EXPECT_NO_FATAL_FAILURE(entityManager.RemoveComponentUVE<PositionComponentUVE>(live));
    EXPECT_TRUE(entityManager.IsAliveUVE(live));
    EXPECT_FALSE(entityManager.HasComponentUVE<PositionComponentUVE>(live));
}
#endif

#if UVE_DEBUG
TEST_F(EntityManagerUVETest, GetComponentPointerUVE_InvalidRequestsAsserts) {
    const EntityUVE stale = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(stale);
    EXPECT_DEATH({ static_cast<void>(entityManager.GetComponentPointerUVE(stale, std::type_index(typeid(PositionComponentUVE)))); }, "");

    const EntityUVE live = entityManager.CreateEntityUVE();
    EXPECT_DEATH({ static_cast<void>(entityManager.GetComponentPointerUVE(live, std::type_index(typeid(PositionComponentUVE)))); }, "");
}
#else
TEST_F(EntityManagerUVETest, GetComponentPointerUVE_InvalidRequestsReturnNull) {
    const EntityUVE stale = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(stale);
    EXPECT_EQ(entityManager.GetComponentPointerUVE(stale, std::type_index(typeid(PositionComponentUVE))), nullptr);

    const EntityUVE live = entityManager.CreateEntityUVE();
    EXPECT_EQ(entityManager.GetComponentPointerUVE(live, std::type_index(typeid(PositionComponentUVE))), nullptr);
}
#endif

TEST_F(EntityManagerUVETest, AddGetHasRemoveComponent_PlainData_RoundTrips) {
    const EntityUVE entity = entityManager.CreateEntityUVE();

    EXPECT_FALSE(entityManager.HasComponentUVE<PositionComponentUVE>(entity));
    PositionComponentUVE& added = entityManager.AddComponentUVE<PositionComponentUVE>(entity);
    added.value = 42;

    EXPECT_TRUE(entityManager.HasComponentUVE<PositionComponentUVE>(entity));
    EXPECT_EQ(entityManager.GetComponentUVE<PositionComponentUVE>(entity).value, 42);

    entityManager.RemoveComponentUVE<PositionComponentUVE>(entity);
    EXPECT_FALSE(entityManager.HasComponentUVE<PositionComponentUVE>(entity));
}

TEST_F(EntityManagerUVETest, AddGetRemoveComponent_NonTrivialType_RoundTrips) {
    const EntityUVE entity = entityManager.CreateEntityUVE();

    entityManager.AddComponentUVE<NonTrivialComponentUVE>(entity, "hello");
    EXPECT_EQ(entityManager.GetComponentUVE<NonTrivialComponentUVE>(entity).name, "hello");

    entityManager.RemoveComponentUVE<NonTrivialComponentUVE>(entity);
    EXPECT_FALSE(entityManager.HasComponentUVE<NonTrivialComponentUVE>(entity));
}

TEST_F(EntityManagerUVETest, ArchetypeMigration_DoesNotCorruptUnrelatedEntity) {
    const EntityUVE entityA = entityManager.CreateEntityUVE();
    const EntityUVE entityB = entityManager.CreateEntityUVE();

    entityManager.AddComponentUVE<PositionComponentUVE>(entityA).value = 1;
    entityManager.AddComponentUVE<PositionComponentUVE>(entityB).value = 2;

    // entityA migrates to a new archetype (Position + Tag); entityB must be unaffected.
    entityManager.AddComponentUVE<TagComponentUVE>(entityA).value = 100;

    EXPECT_EQ(entityManager.GetComponentUVE<PositionComponentUVE>(entityA).value, 1);
    EXPECT_EQ(entityManager.GetComponentUVE<TagComponentUVE>(entityA).value, 100);
    EXPECT_EQ(entityManager.GetComponentUVE<PositionComponentUVE>(entityB).value, 2);
    EXPECT_FALSE(entityManager.HasComponentUVE<TagComponentUVE>(entityB));

    entityManager.RemoveComponentUVE<TagComponentUVE>(entityA);
    EXPECT_EQ(entityManager.GetComponentUVE<PositionComponentUVE>(entityA).value, 1);
    EXPECT_EQ(entityManager.GetComponentUVE<PositionComponentUVE>(entityB).value, 2);
}

TEST_F(EntityManagerUVETest, SwapRemove_DestroyingMiddleEntity_KeepsSurvivorsCorrect) {
    std::vector<EntityUVE> entities;
    constexpr int kEntityCount = 10;
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
        entities.push_back(entity);
    }

    // Destroy one from the middle.
    entityManager.DestroyEntityUVE(entities[5]);
    entities.erase(entities.begin() + 5);

    for (std::size_t i = 0; i < entities.size(); ++i) {
        ASSERT_TRUE(entityManager.IsAliveUVE(entities[i]));
    }
    // Every surviving entity's own value must still match what it was created with (a
    // corrupted swap-remove would show up as a mismatched value somewhere here).
    std::vector<int> observedValues;
    for (const EntityUVE& entity : entities) {
        observedValues.push_back(entityManager.GetComponentUVE<PositionComponentUVE>(entity).value);
    }
    const std::vector<int> expectedValues{0, 1, 2, 3, 4, 6, 7, 8, 9};
    // Order is not guaranteed by swap-remove, so compare as a multiset via sorting.
    std::vector<int> sortedObserved = observedValues;
    std::vector<int> sortedExpected = expectedValues;
    std::sort(sortedObserved.begin(), sortedObserved.end());
    std::sort(sortedExpected.begin(), sortedExpected.end());
    EXPECT_EQ(sortedObserved, sortedExpected);

    // The freed index must be reusable.
    const EntityUVE reused = entityManager.CreateEntityUVE();
    EXPECT_TRUE(entityManager.IsAliveUVE(reused));
}

TEST_F(EntityManagerUVETest, ChunkOverflow_MoreThanOneChunkCapacity_AllEntitiesRemainCorrect) {
    constexpr int kEntityCount = 600; // > kChunkCapacityUVE (512), forces a second chunk
    std::vector<EntityUVE> entities;
    entities.reserve(kEntityCount);
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
        entities.push_back(entity);
    }

    for (int i = 0; i < kEntityCount; ++i) {
        EXPECT_EQ(entityManager.GetComponentUVE<PositionComponentUVE>(entities[static_cast<std::size_t>(i)]).value, i);
    }
}

TEST_F(EntityManagerUVETest, ForEachUVE_VisitsOnlyEntitiesWithAllRequestedComponents) {
    const EntityUVE onlyPosition = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionComponentUVE>(onlyPosition).value = 1;

    const EntityUVE onlyTag = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TagComponentUVE>(onlyTag).value = 2;

    const EntityUVE both = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionComponentUVE>(both).value = 3;
    entityManager.AddComponentUVE<TagComponentUVE>(both).value = 4;

    std::vector<EntityUVE> visited;
    entityManager.ForEachUVE<PositionComponentUVE, TagComponentUVE>(
        [&visited](EntityUVE entity, PositionComponentUVE&, TagComponentUVE&) { visited.push_back(entity); });

    ASSERT_EQ(visited.size(), 1U);
    EXPECT_EQ(visited[0], both);
}

TEST_F(EntityManagerUVETest, EntityCreatedAndDestroyedEvents_ArePublished) {
    EntityUVE createdEntity = kInvalidEntityUVE;
    EntityUVE destroyedEntity = kInvalidEntityUVE;
    eventSystem.Subscribe<EntityCreatedEventUVE>(
        [&createdEntity](const EntityCreatedEventUVE& event) { createdEntity = event.entity; });
    eventSystem.Subscribe<EntityDestroyedEventUVE>(
        [&destroyedEntity](const EntityDestroyedEventUVE& event) { destroyedEntity = event.entity; });

    const EntityUVE entity = entityManager.CreateEntityUVE();
    EXPECT_EQ(createdEntity, entity);

    entityManager.DestroyEntityUVE(entity);
    EXPECT_EQ(destroyedEntity, entity);
}

TEST(EntityManagerUVEDestructorTest, DestructorWithLiveEntities_LeavesNoMemoryLeaks) {
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    {
        EntityManagerUVE entityManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
        for (int i = 0; i < 5; ++i) {
            const EntityUVE entity = entityManager.CreateEntityUVE();
            entityManager.AddComponentUVE<NonTrivialComponentUVE>(entity, "still alive at shutdown");
        }
        // entityManager destructs here without any explicit DestroyEntityUVE() calls.
    }
    EXPECT_FALSE(memoryManager.HasLeaksUVE());
}

// ---------------------------------------------------------------------------
// ForEachErased's chunk-hoisted iteration.
//
// The walk resolves component columns once per CHUNK rather than once per row,
// and reuses one pointer buffer instead of heap-allocating per entity - 535us
// to 31us per walk at 10000 entities, measured on this ECS. Behaviour is meant
// to be identical, so these tests target the ways a hoist can silently go
// wrong rather than the speed: reused buffers leaking state between chunks,
// column pointers resolved against the wrong chunk, and component ORDER (the
// callback unpacks by position, so a swapped pair is a type-confused
// reinterpret_cast, not a compile error).
// ---------------------------------------------------------------------------

TEST_F(EntityManagerUVETest, ForEachUVE_AcrossManyChunks_VisitsEveryEntityExactlyOnce) {
    // Deliberately several times kChunkCapacityUVE, because the hoist is per chunk: a bug that
    // reuses the first chunk's column bases for every chunk only shows up past the first boundary,
    // and would read another chunk's memory rather than fail.
    constexpr int kEntityCount = 1700;
    std::vector<EntityUVE> created;
    created.reserve(kEntityCount);
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
        entityManager.AddComponentUVE<TagComponentUVE>(entity).value = -i;
        created.push_back(entity);
    }

    std::vector<EntityUVE> visited;
    std::vector<int> positions;
    entityManager.ForEachUVE<PositionComponentUVE, TagComponentUVE>(
        [&](EntityUVE entity, PositionComponentUVE& position, TagComponentUVE& tag) {
            visited.push_back(entity);
            positions.push_back(position.value);
            // Each entity's two components must agree - proof the two columns were resolved
            // against the same chunk and row, not mixed across chunks.
            EXPECT_EQ(tag.value, -position.value);
        });

    ASSERT_EQ(visited.size(), static_cast<std::size_t>(kEntityCount));
    std::sort(positions.begin(), positions.end());
    for (int i = 0; i < kEntityCount; ++i) {
        EXPECT_EQ(positions[static_cast<std::size_t>(i)], i);
    }
    std::vector<EntityUVE> sortedVisited = visited;
    std::sort(sortedVisited.begin(), sortedVisited.end(),
              [](EntityUVE lhs, EntityUVE rhs) { return lhs.index < rhs.index; });
    EXPECT_TRUE(std::adjacent_find(sortedVisited.begin(), sortedVisited.end()) == sortedVisited.end())
        << "no entity may be visited twice";
}

TEST_F(EntityManagerUVETest, ForEachUVE_WritesThroughToTheRealComponentStorage) {
    // The hoisted pointers must address the live chunk buffers, not a copy. A hoist that handed
    // back pointers into scratch memory would let every read look right while writes vanished.
    constexpr int kEntityCount = 900;
    std::vector<EntityUVE> created;
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
        created.push_back(entity);
    }

    entityManager.ForEachUVE<PositionComponentUVE>(
        [](EntityUVE, PositionComponentUVE& position) { position.value += 1000; });

    for (int i = 0; i < kEntityCount; ++i) {
        EXPECT_EQ(entityManager.GetComponentUVE<PositionComponentUVE>(created[static_cast<std::size_t>(i)]).value,
                  i + 1000);
    }
}

TEST_F(EntityManagerUVETest, ForEachUVE_ComponentOrderFollowsTheTemplateArgumentsNotStorageOrder) {
    // The callback unpacks the pointer buffer BY POSITION, so if the hoist ever filled it in a
    // different order than the template arguments, each component would be reinterpret_cast as the
    // other - silently, with no compile error. Requested both ways round to pin the mapping.
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = 11;
    entityManager.AddComponentUVE<TagComponentUVE>(entity).value = 22;

    int seenPosition = 0;
    int seenTag = 0;
    entityManager.ForEachUVE<PositionComponentUVE, TagComponentUVE>(
        [&](EntityUVE, PositionComponentUVE& position, TagComponentUVE& tag) {
            seenPosition = position.value;
            seenTag = tag.value;
        });
    EXPECT_EQ(seenPosition, 11);
    EXPECT_EQ(seenTag, 22);

    seenPosition = 0;
    seenTag = 0;
    entityManager.ForEachUVE<TagComponentUVE, PositionComponentUVE>(
        [&](EntityUVE, TagComponentUVE& tag, PositionComponentUVE& position) {
            seenPosition = position.value;
            seenTag = tag.value;
        });
    EXPECT_EQ(seenPosition, 11);
    EXPECT_EQ(seenTag, 22);
}

TEST_F(EntityManagerUVETest, ForEachUVE_RepeatedWalksAreIdentical) {
    // The pointer and column-view buffers are reused across chunks AND across calls now. If either
    // leaked state - a stale entry from a wider previous query, say - the second walk would differ
    // from the first.
    constexpr int kEntityCount = 1200;
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
        entityManager.AddComponentUVE<TagComponentUVE>(entity).value = i * 2;
    }

    const auto collectUVE = [this]() {
        std::vector<int> values;
        entityManager.ForEachUVE<PositionComponentUVE, TagComponentUVE>(
            [&values](EntityUVE, PositionComponentUVE& position, TagComponentUVE& tag) {
                values.push_back(position.value);
                values.push_back(tag.value);
            });
        return values;
    };

    const std::vector<int> first = collectUVE();
    const std::vector<int> second = collectUVE();
    EXPECT_EQ(first, second);
    EXPECT_EQ(first.size(), static_cast<std::size_t>(kEntityCount) * 2U);
}

TEST_F(EntityManagerUVETest, ForEachUVE_AfterAWiderQuery_ANarrowerOneIsNotPolluted) {
    // The buffers are sized per call but reused across calls. A two-component walk followed by a
    // one-component walk must not leave the second reading the first's extra column.
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = 7;
    entityManager.AddComponentUVE<TagComponentUVE>(entity).value = 8;

    std::size_t wideVisits = 0U;
    entityManager.ForEachUVE<PositionComponentUVE, TagComponentUVE>(
        [&wideVisits](EntityUVE, PositionComponentUVE&, TagComponentUVE&) { ++wideVisits; });
    ASSERT_EQ(wideVisits, 1U);

    int narrowValue = 0;
    std::size_t narrowVisits = 0U;
    entityManager.ForEachUVE<PositionComponentUVE>([&](EntityUVE, PositionComponentUVE& position) {
        narrowValue = position.value;
        ++narrowVisits;
    });
    EXPECT_EQ(narrowVisits, 1U);
    EXPECT_EQ(narrowValue, 7);
}

TEST_F(EntityManagerUVETest, ForEachUVE_PartiallyFilledFinalChunk_VisitsOnlyOccupiedRows) {
    // The hoist reads each chunk's occupied count once. Using the chunk capacity instead of the
    // live count would walk uninitialized rows in the final, partly-filled chunk.
    constexpr int kEntityCount = 513; // one full chunk plus a single row
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
    }

    std::size_t visits = 0U;
    entityManager.ForEachUVE<PositionComponentUVE>([&visits](EntityUVE, PositionComponentUVE&) { ++visits; });
    EXPECT_EQ(visits, static_cast<std::size_t>(kEntityCount));
}

TEST_F(EntityManagerUVETest, ForEachUVE_AfterDestructionCompactsChunks_VisitsSurvivorsOnly) {
    // Destroying entities vacates rows and moves survivors between rows. The hoisted bases must
    // reflect the post-compaction layout - a walk holding pre-destruction pointers would report
    // dead entities' values.
    constexpr int kEntityCount = 800;
    std::vector<EntityUVE> created;
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
        created.push_back(entity);
    }
    // Destroy every other entity, so compaction genuinely shuffles rows around.
    for (std::size_t index = 0U; index < created.size(); index += 2U) {
        entityManager.DestroyEntityUVE(created[index]);
    }

    std::vector<int> survivors;
    entityManager.ForEachUVE<PositionComponentUVE>(
        [&survivors](EntityUVE, PositionComponentUVE& position) { survivors.push_back(position.value); });

    ASSERT_EQ(survivors.size(), static_cast<std::size_t>(kEntityCount) / 2U);
    std::sort(survivors.begin(), survivors.end());
    for (std::size_t index = 0U; index < survivors.size(); ++index) {
        EXPECT_EQ(survivors[index], static_cast<int>(index) * 2 + 1) << "only odd-valued entities survived";
    }
}

TEST_F(EntityManagerUVETest, ForEachUVE_NonTrivialComponents_AreVisitedIntact) {
    // Column stride comes from ComponentTypeInfoUVE::size. A non-trivial type with a different
    // size than the trivial test components catches a stride taken from the wrong column.
    constexpr int kEntityCount = 600;
    for (int i = 0; i < kEntityCount; ++i) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<NonTrivialComponentUVE>(entity).name = "entity_" + std::to_string(i);
        entityManager.AddComponentUVE<PositionComponentUVE>(entity).value = i;
    }

    std::size_t visits = 0U;
    entityManager.ForEachUVE<NonTrivialComponentUVE, PositionComponentUVE>(
        [&visits](EntityUVE, NonTrivialComponentUVE& nonTrivial, PositionComponentUVE& position) {
            EXPECT_EQ(nonTrivial.name, "entity_" + std::to_string(position.value));
            ++visits;
        });
    EXPECT_EQ(visits, static_cast<std::size_t>(kEntityCount));
}

} // namespace
} // namespace UVE::Scene::Tests
