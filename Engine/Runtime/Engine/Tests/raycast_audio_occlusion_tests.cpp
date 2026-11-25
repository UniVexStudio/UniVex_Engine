// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "raycast_audio_occlusion.h"

#include <cmath>

#include <gtest/gtest.h>

#include "collider_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "raycast_system.h"
#include "scene_graph.h"

namespace UVE::Core::Tests {
namespace {

/// The adapter is the only place the audio module touches physics, so this suite exercises it
/// against the real entity manager, the real scene graph and the real raycast system - no fake
/// collider world, because the question "does a wall between us muffle this" is exactly the
/// question a fake would answer for itself.
class RaycastAudioOcclusionTest : public ::testing::Test {
protected:
    /// A wall 5 m down -Z, 2 m wide, thin in Z. The default listener faces -Z.
    Scene::Entity MakeWall() {
        return MakeBox(Math::Vector3{0.0F, 0.0F, -5.0F}, Math::Vector3{1.0F, 1.0F, 0.05F},
                          kWallLayer);
    }

    Scene::Entity MakeBox(const Math::Vector3& position, const Math::Vector3& halfExtents,
                                const std::uint32_t layer) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform;
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
        Scene::ColliderComponent collider;
        collider.halfExtents = halfExtents;
        collider.collisionLayer = layer;
        entityManager.AddComponent<Scene::ColliderComponent>(entity, collider);
        return entity;
    }

    [[nodiscard]] Audio::AudioOcclusionQuery Query(const Math::Vector3& listener,
                                                         const Math::Vector3& source) const {
        Audio::AudioOcclusionQuery query;
        query.listenerPosition = listener;
        query.sourcePosition = source;
        query.listenerRight = Math::Vector3{1.0F, 0.0F, 0.0F};
        return query;
    }

    static constexpr std::uint32_t kWallLayer = 2U;
    /// Layer 1 is the default for everything that is not a wall in these cases.
    static constexpr std::uint32_t kEverything = 0xFFFFFFFFU;

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    Physics::RaycastSystem raycastSystem;
};

TEST_F(RaycastAudioOcclusionTest, AnEmptyWorldNeverOccludes) {
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(Query(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -10.0F})),
                    0.0F);
}

TEST_F(RaycastAudioOcclusionTest, AWallBetweenListenerAndSourceBlocksThePath) {
    static_cast<void>(MakeWall());
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(Query(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -10.0F})),
                    1.0F);
}

TEST_F(RaycastAudioOcclusionTest, AWallBehindTheSourceBlocksNothing) {
    // Same wall, listener facing the other way: the source at +Z is in clear air.
    static_cast<void>(MakeWall());
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(Query(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, 10.0F})),
                    0.0F);
}

TEST_F(RaycastAudioOcclusionTest, AnEmitterIsNotBlockedByItsOwnCollider) {
    const Scene::Entity wall = MakeWall();
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    Audio::AudioOcclusionQuery query = Query(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -10.0F});
    query.sourceEntity = wall;
    // The wall is the emitter's own body, so it cannot be what blocks the emitter.
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(query), 0.0F);
}

TEST_F(RaycastAudioOcclusionTest, TheLayerMaskDecidesWhatCountsAsABlocker) {
    static_cast<void>(MakeWall());
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    Audio::AudioOcclusionQuery query = Query(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -10.0F});

    query.layerMask = kWallLayer;
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(query), 1.0F);
    query.layerMask = 0x1U; // walls are not on this layer
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(query), 0.0F);
    query.layerMask = kEverything;
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(query), 1.0F);
}

TEST_F(RaycastAudioOcclusionTest, ASourceOnTopOfTheListenerIsClear) {
    // A degenerate path must not be traced at all - there is no direction to trace along.
    static_cast<void>(MakeWall());
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusion(Query(Math::Vector3{0.0F, 0.0F, 2.0F},
                                                        Math::Vector3{0.0F, 0.0F, 2.0F})),
                    0.0F);
}

TEST_F(RaycastAudioOcclusionTest, APartialBlockerGivesAFractionNotASwitch) {
    // A post that only covers the right ear's path. Halfway to a source 10 m down -Z the right
    // ear's ray is at x = +0.05, the centre at 0 and the left at -0.05; a 4 cm post centred on the
    // right ray therefore blocks exactly one of the three.
    MakeBox(Math::Vector3{0.05F, 0.0F, -5.0F}, Math::Vector3{0.02F, 0.2F, 0.02F}, kEverything);
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    const float occlusion =
        provider.QueryOcclusion(Query(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -10.0F}));
    EXPECT_NEAR(occlusion, 1.0F / 3.0F, 1.0e-6F);
    EXPECT_GT(occlusion, 0.0F);
    EXPECT_LT(occlusion, 1.0F);
}

TEST_F(RaycastAudioOcclusionTest, TheEarOffsetIsWhatMakesPartialBlockagePossible) {
    // A blocker exactly one ear away: with a single centre ray it would be missed entirely.
    MakeBox(Math::Vector3{0.05F, 0.0F, -5.0F}, Math::Vector3{0.02F, 0.2F, 0.02F}, kEverything);
    EXPECT_GT(RaycastAudioOcclusion::kEarOffsetMeters, 0.0F);
    RaycastAudioOcclusion provider{entityManager, raycastSystem};
    EXPECT_GT(provider.QueryOcclusion(Query(Math::Vector3{}, Math::Vector3{0.0F, 0.0F, -10.0F})),
              0.0F);
}

} // namespace
} // namespace UVE::Core::Tests
