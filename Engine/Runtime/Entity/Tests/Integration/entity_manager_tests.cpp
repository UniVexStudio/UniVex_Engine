// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "entity_manager.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "platform.h"
#include "entity_lifecycle_events.h"

namespace UVE::Scene::Tests {
namespace {

struct PositionComponent {
    int value = 0;
};

struct TagComponent {
    int value = 0;
};

struct NonTrivialComponent {
    std::string name;
};

class EntityManagerTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
};

TEST_F(EntityManagerTest, CreateEntity_IsAliveAndCounted) {
    const Entity entity = entityManager.CreateEntity();
    EXPECT_TRUE(entityManager.IsAlive(entity));
    EXPECT_EQ(entityManager.GetEntityCount(), 1U);
}

TEST_F(EntityManagerTest, DestroyEntity_IsNoLongerAlive) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.DestroyEntity(entity);
    EXPECT_FALSE(entityManager.IsAlive(entity));
    EXPECT_EQ(entityManager.GetEntityCount(), 0U);
}

#if UVE_DEBUG
TEST_F(EntityManagerTest, DestroyEntity_StaleHandleAsserts) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.DestroyEntity(entity);
    EXPECT_DEATH({ entityManager.DestroyEntity(entity); }, "");
}

TEST_F(EntityManagerTest, HasComponent_StaleHandleAsserts) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.DestroyEntity(entity);
    EXPECT_DEATH({ static_cast<void>(entityManager.HasComponent<PositionComponent>(entity)); }, "");
}

TEST_F(EntityManagerTest, GetComponentTypes_StaleHandleAsserts) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.DestroyEntity(entity);
    EXPECT_DEATH({ static_cast<void>(entityManager.GetComponentTypes(entity)); }, "");
}
#else
TEST_F(EntityManagerTest, StaleHandleQueriesAndDestroyFailClosed) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.DestroyEntity(entity);

    EXPECT_NO_FATAL_FAILURE(entityManager.DestroyEntity(entity));
    EXPECT_FALSE(entityManager.HasComponent<PositionComponent>(entity));
    EXPECT_TRUE(entityManager.GetComponentTypes(entity).empty());
    EXPECT_EQ(entityManager.GetEntityCount(), 0U);
}
#endif

TEST_F(EntityManagerTest, ReusedIndex_YieldsDifferentGeneration) {
    const Entity first = entityManager.CreateEntity();
    entityManager.DestroyEntity(first);
    const Entity second = entityManager.CreateEntity();

    EXPECT_EQ(first.index, second.index);
    EXPECT_NE(first.generation, second.generation);
    EXPECT_FALSE(entityManager.IsAlive(first));
    EXPECT_TRUE(entityManager.IsAlive(second));
}

#if UVE_DEBUG
TEST_F(EntityManagerTest, RemoveComponent_StaleHandleAsserts) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.DestroyEntity(entity);
    EXPECT_DEATH({ entityManager.RemoveComponent<PositionComponent>(entity); }, "");
}

TEST_F(EntityManagerTest, RemoveComponent_AbsentComponentAsserts) {
    const Entity entity = entityManager.CreateEntity();
    EXPECT_DEATH({ entityManager.RemoveComponent<PositionComponent>(entity); }, "");
}
#else
TEST_F(EntityManagerTest, RemoveComponent_InvalidRequestsFailClosed) {
    const Entity stale = entityManager.CreateEntity();
    entityManager.DestroyEntity(stale);
    EXPECT_NO_FATAL_FAILURE(entityManager.RemoveComponent<PositionComponent>(stale));

    const Entity live = entityManager.CreateEntity();
    EXPECT_NO_FATAL_FAILURE(entityManager.RemoveComponent<PositionComponent>(live));
    EXPECT_TRUE(entityManager.IsAlive(live));
    EXPECT_FALSE(entityManager.HasComponent<PositionComponent>(live));
}
#endif

#if UVE_DEBUG
TEST_F(EntityManagerTest, GetComponentPointer_InvalidRequestsAsserts) {
    const Entity stale = entityManager.CreateEntity();
    entityManager.DestroyEntity(stale);
    EXPECT_DEATH({ static_cast<void>(entityManager.GetComponentPointer(stale, std::type_index(typeid(PositionComponent)))); }, "");

    const Entity live = entityManager.CreateEntity();
    EXPECT_DEATH({ static_cast<void>(entityManager.GetComponentPointer(live, std::type_index(typeid(PositionComponent)))); }, "");
}
#else
TEST_F(EntityManagerTest, GetComponentPointer_InvalidRequestsReturnNull) {
    const Entity stale = entityManager.CreateEntity();
    entityManager.DestroyEntity(stale);
    EXPECT_EQ(entityManager.GetComponentPointer(stale, std::type_index(typeid(PositionComponent))), nullptr);

    const Entity live = entityManager.CreateEntity();
    EXPECT_EQ(entityManager.GetComponentPointer(live, std::type_index(typeid(PositionComponent))), nullptr);
}
#endif

TEST_F(EntityManagerTest, AddGetHasRemoveComponent_PlainData_RoundTrips) {
    const Entity entity = entityManager.CreateEntity();

    EXPECT_FALSE(entityManager.HasComponent<PositionComponent>(entity));
    PositionComponent& added = entityManager.AddComponent<PositionComponent>(entity);
    added.value = 42;

    EXPECT_TRUE(entityManager.HasComponent<PositionComponent>(entity));
    EXPECT_EQ(entityManager.GetComponent<PositionComponent>(entity).value, 42);

    entityManager.RemoveComponent<PositionComponent>(entity);
    EXPECT_FALSE(entityManager.HasComponent<PositionComponent>(entity));
}

TEST_F(EntityManagerTest, AddGetRemoveComponent_NonTrivialType_RoundTrips) {
    const Entity entity = entityManager.CreateEntity();

    entityManager.AddComponent<NonTrivialComponent>(entity, "hello");
    EXPECT_EQ(entityManager.GetComponent<NonTrivialComponent>(entity).name, "hello");

    entityManager.RemoveComponent<NonTrivialComponent>(entity);
    EXPECT_FALSE(entityManager.HasComponent<NonTrivialComponent>(entity));
}

TEST_F(EntityManagerTest, ArchetypeMigration_DoesNotCorruptUnrelatedEntity) {
    const Entity entityA = entityManager.CreateEntity();
    const Entity entityB = entityManager.CreateEntity();

    entityManager.AddComponent<PositionComponent>(entityA).value = 1;
    entityManager.AddComponent<PositionComponent>(entityB).value = 2;

    // entityA migrates to a new archetype (Position + Tag); entityB must be unaffected.
    entityManager.AddComponent<TagComponent>(entityA).value = 100;

    EXPECT_EQ(entityManager.GetComponent<PositionComponent>(entityA).value, 1);
    EXPECT_EQ(entityManager.GetComponent<TagComponent>(entityA).value, 100);
    EXPECT_EQ(entityManager.GetComponent<PositionComponent>(entityB).value, 2);
    EXPECT_FALSE(entityManager.HasComponent<TagComponent>(entityB));

    entityManager.RemoveComponent<TagComponent>(entityA);
    EXPECT_EQ(entityManager.GetComponent<PositionComponent>(entityA).value, 1);
    EXPECT_EQ(entityManager.GetComponent<PositionComponent>(entityB).value, 2);
}

TEST_F(EntityManagerTest, SwapRemove_DestroyingMiddleEntity_KeepsSurvivorsCorrect) {
    std::vector<Entity> entities;
    constexpr int kEntityCount = 10;
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PositionComponent>(entity).value = i;
        entities.push_back(entity);
    }

    // Destroy one from the middle.
    entityManager.DestroyEntity(entities[5]);
    entities.erase(entities.begin() + 5);

    for (std::size_t i = 0; i < entities.size(); ++i) {
        ASSERT_TRUE(entityManager.IsAlive(entities[i]));
    }
    // Every surviving entity's own value must still match what it was created with (a
    // corrupted swap-remove would show up as a mismatched value somewhere here).
    std::vector<int> observedValues;
    for (const Entity& entity : entities) {
        observedValues.push_back(entityManager.GetComponent<PositionComponent>(entity).value);
    }
    const std::vector<int> expectedValues{0, 1, 2, 3, 4, 6, 7, 8, 9};
    // Order is not guaranteed by swap-remove, so compare as a multiset via sorting.
    std::vector<int> sortedObserved = observedValues;
    std::vector<int> sortedExpected = expectedValues;
    std::sort(sortedObserved.begin(), sortedObserved.end());
    std::sort(sortedExpected.begin(), sortedExpected.end());
    EXPECT_EQ(sortedObserved, sortedExpected);

    // The freed index must be reusable.
    const Entity reused = entityManager.CreateEntity();
    EXPECT_TRUE(entityManager.IsAlive(reused));
}

TEST_F(EntityManagerTest, ChunkOverflow_MoreThanOneChunkCapacity_AllEntitiesRemainCorrect) {
    constexpr int kEntityCount = 600; // > kChunkCapacity (512), forces a second chunk
    std::vector<Entity> entities;
    entities.reserve(kEntityCount);
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PositionComponent>(entity).value = i;
        entities.push_back(entity);
    }

    for (int i = 0; i < kEntityCount; ++i) {
        EXPECT_EQ(entityManager.GetComponent<PositionComponent>(entities[static_cast<std::size_t>(i)]).value, i);
    }
}

TEST_F(EntityManagerTest, ForEach_VisitsOnlyEntitiesWithAllRequestedComponents) {
    const Entity onlyPosition = entityManager.CreateEntity();
    entityManager.AddComponent<PositionComponent>(onlyPosition).value = 1;

    const Entity onlyTag = entityManager.CreateEntity();
    entityManager.AddComponent<TagComponent>(onlyTag).value = 2;

    const Entity both = entityManager.CreateEntity();
    entityManager.AddComponent<PositionComponent>(both).value = 3;
    entityManager.AddComponent<TagComponent>(both).value = 4;

    std::vector<Entity> visited;
    entityManager.ForEach<PositionComponent, TagComponent>(
        [&visited](Entity entity, PositionComponent&, TagComponent&) { visited.push_back(entity); });

    ASSERT_EQ(visited.size(), 1U);
    EXPECT_EQ(visited[0], both);
}

TEST_F(EntityManagerTest, EntityCreatedAndDestroyedEvents_ArePublished) {
    Entity createdEntity = kInvalidEntity;
    Entity destroyedEntity = kInvalidEntity;
    eventSystem.Subscribe<EntityCreatedEvent>(
        [&createdEntity](const EntityCreatedEvent& event) { createdEntity = event.entity; });
    eventSystem.Subscribe<EntityDestroyedEvent>(
        [&destroyedEntity](const EntityDestroyedEvent& event) { destroyedEntity = event.entity; });

    const Entity entity = entityManager.CreateEntity();
    EXPECT_EQ(createdEntity, entity);

    entityManager.DestroyEntity(entity);
    EXPECT_EQ(destroyedEntity, entity);
}

TEST(EntityManagerDestructorTest, DestructorWithLiveEntities_LeavesNoMemoryLeaks) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    {
        EntityManager entityManager(memoryManager.GetDefaultAllocator(), eventSystem);
        for (int i = 0; i < 5; ++i) {
            const Entity entity = entityManager.CreateEntity();
            entityManager.AddComponent<NonTrivialComponent>(entity, "still alive at shutdown");
        }
        // entityManager destructs here without any explicit DestroyEntity() calls.
    }
    EXPECT_FALSE(memoryManager.HasLeaks());
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

TEST_F(EntityManagerTest, ForEach_AcrossManyChunks_VisitsEveryEntityExactlyOnce) {
    // Deliberately several times kChunkCapacity, because the hoist is per chunk: a bug that
    // reuses the first chunk's column bases for every chunk only shows up past the first boundary,
    // and would read another chunk's memory rather than fail.
    constexpr int kEntityCount = 1700;
    std::vector<Entity> created;
    created.reserve(kEntityCount);
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PositionComponent>(entity).value = i;
        entityManager.AddComponent<TagComponent>(entity).value = -i;
        created.push_back(entity);
    }

    std::vector<Entity> visited;
    std::vector<int> positions;
    entityManager.ForEach<PositionComponent, TagComponent>(
        [&](Entity entity, PositionComponent& position, TagComponent& tag) {
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
    std::vector<Entity> sortedVisited = visited;
    std::sort(sortedVisited.begin(), sortedVisited.end(),
              [](Entity lhs, Entity rhs) { return lhs.index < rhs.index; });
    EXPECT_TRUE(std::adjacent_find(sortedVisited.begin(), sortedVisited.end()) == sortedVisited.end())
        << "no entity may be visited twice";
}

TEST_F(EntityManagerTest, ForEach_WritesThroughToTheRealComponentStorage) {
    // The hoisted pointers must address the live chunk buffers, not a copy. A hoist that handed
    // back pointers into scratch memory would let every read look right while writes vanished.
    constexpr int kEntityCount = 900;
    std::vector<Entity> created;
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PositionComponent>(entity).value = i;
        created.push_back(entity);
    }

    entityManager.ForEach<PositionComponent>(
        [](Entity, PositionComponent& position) { position.value += 1000; });

    for (int i = 0; i < kEntityCount; ++i) {
        EXPECT_EQ(entityManager.GetComponent<PositionComponent>(created[static_cast<std::size_t>(i)]).value,
                  i + 1000);
    }
}

TEST_F(EntityManagerTest, ForEach_ComponentOrderFollowsTheTemplateArgumentsNotStorageOrder) {
    // The callback unpacks the pointer buffer BY POSITION, so if the hoist ever filled it in a
    // different order than the template arguments, each component would be reinterpret_cast as the
    // other - silently, with no compile error. Requested both ways round to pin the mapping.
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<PositionComponent>(entity).value = 11;
    entityManager.AddComponent<TagComponent>(entity).value = 22;

    int seenPosition = 0;
    int seenTag = 0;
    entityManager.ForEach<PositionComponent, TagComponent>(
        [&](Entity, PositionComponent& position, TagComponent& tag) {
            seenPosition = position.value;
            seenTag = tag.value;
        });
    EXPECT_EQ(seenPosition, 11);
    EXPECT_EQ(seenTag, 22);

    seenPosition = 0;
    seenTag = 0;
    entityManager.ForEach<TagComponent, PositionComponent>(
        [&](Entity, TagComponent& tag, PositionComponent& position) {
            seenPosition = position.value;
            seenTag = tag.value;
        });
    EXPECT_EQ(seenPosition, 11);
    EXPECT_EQ(seenTag, 22);
}

TEST_F(EntityManagerTest, ForEach_RepeatedWalksAreIdentical) {
    // The pointer and column-view buffers are reused across chunks AND across calls now. If either
    // leaked state - a stale entry from a wider previous query, say - the second walk would differ
    // from the first.
    constexpr int kEntityCount = 1200;
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PositionComponent>(entity).value = i;
        entityManager.AddComponent<TagComponent>(entity).value = i * 2;
    }

    const auto collect = [this]() {
        std::vector<int> values;
        entityManager.ForEach<PositionComponent, TagComponent>(
            [&values](Entity, PositionComponent& position, TagComponent& tag) {
                values.push_back(position.value);
                values.push_back(tag.value);
            });
        return values;
    };

    const std::vector<int> first = collect();
    const std::vector<int> second = collect();
    EXPECT_EQ(first, second);
    EXPECT_EQ(first.size(), static_cast<std::size_t>(kEntityCount) * 2U);
}

TEST_F(EntityManagerTest, ForEach_AfterAWiderQuery_ANarrowerOneIsNotPolluted) {
    // The buffers are sized per call but reused across calls. A two-component walk followed by a
    // one-component walk must not leave the second reading the first's extra column.
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<PositionComponent>(entity).value = 7;
    entityManager.AddComponent<TagComponent>(entity).value = 8;

    std::size_t wideVisits = 0U;
    entityManager.ForEach<PositionComponent, TagComponent>(
        [&wideVisits](Entity, PositionComponent&, TagComponent&) { ++wideVisits; });
    ASSERT_EQ(wideVisits, 1U);

    int narrowValue = 0;
    std::size_t narrowVisits = 0U;
    entityManager.ForEach<PositionComponent>([&](Entity, PositionComponent& position) {
        narrowValue = position.value;
        ++narrowVisits;
    });
    EXPECT_EQ(narrowVisits, 1U);
    EXPECT_EQ(narrowValue, 7);
}

TEST_F(EntityManagerTest, ForEach_PartiallyFilledFinalChunk_VisitsOnlyOccupiedRows) {
    // The hoist reads each chunk's occupied count once. Using the chunk capacity instead of the
    // live count would walk uninitialized rows in the final, partly-filled chunk.
    constexpr int kEntityCount = 513; // one full chunk plus a single row
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PositionComponent>(entity).value = i;
    }

    std::size_t visits = 0U;
    entityManager.ForEach<PositionComponent>([&visits](Entity, PositionComponent&) { ++visits; });
    EXPECT_EQ(visits, static_cast<std::size_t>(kEntityCount));
}

TEST_F(EntityManagerTest, ForEach_AfterDestructionCompactsChunks_VisitsSurvivorsOnly) {
    // Destroying entities vacates rows and moves survivors between rows. The hoisted bases must
    // reflect the post-compaction layout - a walk holding pre-destruction pointers would report
    // dead entities' values.
    constexpr int kEntityCount = 800;
    std::vector<Entity> created;
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<PositionComponent>(entity).value = i;
        created.push_back(entity);
    }
    // Destroy every other entity, so compaction genuinely shuffles rows around.
    for (std::size_t index = 0U; index < created.size(); index += 2U) {
        entityManager.DestroyEntity(created[index]);
    }

    std::vector<int> survivors;
    entityManager.ForEach<PositionComponent>(
        [&survivors](Entity, PositionComponent& position) { survivors.push_back(position.value); });

    ASSERT_EQ(survivors.size(), static_cast<std::size_t>(kEntityCount) / 2U);
    std::sort(survivors.begin(), survivors.end());
    for (std::size_t index = 0U; index < survivors.size(); ++index) {
        EXPECT_EQ(survivors[index], static_cast<int>(index) * 2 + 1) << "only odd-valued entities survived";
    }
}

TEST_F(EntityManagerTest, ForEach_NonTrivialComponents_AreVisitedIntact) {
    // Column stride comes from ComponentTypeInfo::size. A non-trivial type with a different
    // size than the trivial test components catches a stride taken from the wrong column.
    constexpr int kEntityCount = 600;
    for (int i = 0; i < kEntityCount; ++i) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<NonTrivialComponent>(entity).name = "entity_" + std::to_string(i);
        entityManager.AddComponent<PositionComponent>(entity).value = i;
    }

    std::size_t visits = 0U;
    entityManager.ForEach<NonTrivialComponent, PositionComponent>(
        [&visits](Entity, NonTrivialComponent& nonTrivial, PositionComponent& position) {
            EXPECT_EQ(nonTrivial.name, "entity_" + std::to_string(position.value));
            ++visits;
        });
    EXPECT_EQ(visits, static_cast<std::size_t>(kEntityCount));
}

TEST_F(EntityManagerTest, DestroyEntity_ReentrantDestroyAndCreateInListenerRemainsSafe) {
    const Entity target = entityManager.CreateEntity();
    entityManager.AddComponent<PositionComponent>(target).value = 42;

    int listenerCalls = 0;
    std::vector<Entity> spawnedInListener;
    eventSystem.Subscribe<EntityDestroyedEvent>([&](const EntityDestroyedEvent& event) {
        if (event.entity != target) {
            return;
        }
        ++listenerCalls;
        // Re-entrantly destroying the same entity must be a no-op.
        entityManager.DestroyEntity(target);
        // Creating many entities forces m_records vector reallocation while DestroyEntity is active.
        for (int i = 0; i < 64; ++i) {
            const Entity e = entityManager.CreateEntity();
            entityManager.AddComponent<PositionComponent>(e).value = i;
            spawnedInListener.push_back(e);
        }
    });

    entityManager.DestroyEntity(target);
    EXPECT_EQ(listenerCalls, 1);
    EXPECT_FALSE(entityManager.IsAlive(target));
    ASSERT_EQ(spawnedInListener.size(), 64U);
    for (int i = 0; i < 64; ++i) {
        const Entity e = spawnedInListener[static_cast<std::size_t>(i)];
        EXPECT_TRUE(entityManager.IsAlive(e));
        EXPECT_EQ(entityManager.GetComponent<PositionComponent>(e).value, i);
    }
}

} // namespace
} // namespace UVE::Scene::Tests
