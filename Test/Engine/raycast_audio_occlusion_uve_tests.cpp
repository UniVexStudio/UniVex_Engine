// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/raycast_audio_occlusion_uve.h"

#include <cmath>

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/physics/raycast_system_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Core::Tests {
namespace {

/// The adapter is the only place the audio module touches physics, so this suite exercises it
/// against the real entity manager, the real scene graph and the real raycast system - no fake
/// collider world, because the question "does a wall between us muffle this" is exactly the
/// question a fake would answer for itself.
class RaycastAudioOcclusionUVETest : public ::testing::Test {
protected:
    /// A wall 5 m down -Z, 2 m wide, thin in Z. The default listener faces -Z.
    Scene::EntityUVE MakeWallUVE() {
        return MakeBoxUVE(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.05F},
                          kWallLayerUVE);
    }

    Scene::EntityUVE MakeBoxUVE(const Math::Vector3UVE& position, const Math::Vector3UVE& halfExtents,
                                const std::uint32_t layer) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform;
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
        Scene::ColliderComponentUVE collider;
        collider.halfExtents = halfExtents;
        collider.collisionLayer = layer;
        entityManager.AddComponentUVE<Scene::ColliderComponentUVE>(entity, collider);
        return entity;
    }

    [[nodiscard]] Audio::AudioOcclusionQueryUVE QueryUVE(const Math::Vector3UVE& listener,
                                                         const Math::Vector3UVE& source) const {
        Audio::AudioOcclusionQueryUVE query;
        query.listenerPosition = listener;
        query.sourcePosition = source;
        query.listenerRight = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        return query;
    }

    static constexpr std::uint32_t kWallLayerUVE = 2U;
    /// Layer 1 is the default for everything that is not a wall in these cases.
    static constexpr std::uint32_t kEverythingUVE = 0xFFFFFFFFU;

    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    Physics::RaycastSystemUVE raycastSystem;
};

TEST_F(RaycastAudioOcclusionUVETest, AnEmptyWorldNeverOccludes) {
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(QueryUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -10.0F})),
                    0.0F);
}

TEST_F(RaycastAudioOcclusionUVETest, AWallBetweenListenerAndSourceBlocksThePath) {
    static_cast<void>(MakeWallUVE());
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(QueryUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -10.0F})),
                    1.0F);
}

TEST_F(RaycastAudioOcclusionUVETest, AWallBehindTheSourceBlocksNothing) {
    // Same wall, listener facing the other way: the source at +Z is in clear air.
    static_cast<void>(MakeWallUVE());
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(QueryUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, 10.0F})),
                    0.0F);
}

TEST_F(RaycastAudioOcclusionUVETest, AnEmitterIsNotBlockedByItsOwnCollider) {
    const Scene::EntityUVE wall = MakeWallUVE();
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    Audio::AudioOcclusionQueryUVE query = QueryUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -10.0F});
    query.sourceEntity = wall;
    // The wall is the emitter's own body, so it cannot be what blocks the emitter.
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(query), 0.0F);
}

TEST_F(RaycastAudioOcclusionUVETest, TheLayerMaskDecidesWhatCountsAsABlocker) {
    static_cast<void>(MakeWallUVE());
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    Audio::AudioOcclusionQueryUVE query = QueryUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -10.0F});

    query.layerMask = kWallLayerUVE;
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(query), 1.0F);
    query.layerMask = 0x1U; // walls are not on this layer
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(query), 0.0F);
    query.layerMask = kEverythingUVE;
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(query), 1.0F);
}

TEST_F(RaycastAudioOcclusionUVETest, ASourceOnTopOfTheListenerIsClear) {
    // A degenerate path must not be traced at all - there is no direction to trace along.
    static_cast<void>(MakeWallUVE());
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    EXPECT_FLOAT_EQ(provider.QueryOcclusionUVE(QueryUVE(Math::Vector3UVE{0.0F, 0.0F, 2.0F},
                                                        Math::Vector3UVE{0.0F, 0.0F, 2.0F})),
                    0.0F);
}

TEST_F(RaycastAudioOcclusionUVETest, APartialBlockerGivesAFractionNotASwitch) {
    // A post that only covers the right ear's path. Halfway to a source 10 m down -Z the right
    // ear's ray is at x = +0.05, the centre at 0 and the left at -0.05; a 4 cm post centred on the
    // right ray therefore blocks exactly one of the three.
    MakeBoxUVE(Math::Vector3UVE{0.05F, 0.0F, -5.0F}, Math::Vector3UVE{0.02F, 0.2F, 0.02F}, kEverythingUVE);
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    const float occlusion =
        provider.QueryOcclusionUVE(QueryUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -10.0F}));
    EXPECT_NEAR(occlusion, 1.0F / 3.0F, 1.0e-6F);
    EXPECT_GT(occlusion, 0.0F);
    EXPECT_LT(occlusion, 1.0F);
}

TEST_F(RaycastAudioOcclusionUVETest, TheEarOffsetIsWhatMakesPartialBlockagePossible) {
    // A blocker exactly one ear away: with a single centre ray it would be missed entirely.
    MakeBoxUVE(Math::Vector3UVE{0.05F, 0.0F, -5.0F}, Math::Vector3UVE{0.02F, 0.2F, 0.02F}, kEverythingUVE);
    EXPECT_GT(RaycastAudioOcclusionUVE::kEarOffsetMetersUVE, 0.0F);
    RaycastAudioOcclusionUVE provider{entityManager, raycastSystem};
    EXPECT_GT(provider.QueryOcclusionUVE(QueryUVE(Math::Vector3UVE{}, Math::Vector3UVE{0.0F, 0.0F, -10.0F})),
              0.0F);
}

} // namespace
} // namespace UVE::Core::Tests
