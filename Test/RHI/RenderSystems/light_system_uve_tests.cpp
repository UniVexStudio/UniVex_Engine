// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/light_system_uve.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include <gtest/gtest.h>

#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Render::Tests {
namespace {

class LightSystemUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    LightSystemUVE lightSystem;

    [[nodiscard]] Scene::EntityUVE MakeLightEntityUVE(Math::Vector3UVE position, Math::QuaternionUVE rotation,
                                                        Scene::LightComponentUVE light) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransformUVE(entityManager, entity, local);
        sceneGraph.UpdateUVE(entityManager);
        entityManager.AddComponentUVE<Scene::LightComponentUVE>(entity, light);
        return entity;
    }
};

TEST(LightComponentUVETest, IsLightComponentValidUVE_RejectsUnsafeValues) {
    EXPECT_TRUE(Scene::IsLightComponentValidUVE(Scene::LightComponentUVE{}));

    Scene::LightComponentUVE invalid = {};
    invalid.color.x = -0.1F;
    EXPECT_FALSE(Scene::IsLightComponentValidUVE(invalid));
    invalid = {};
    invalid.color.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsLightComponentValidUVE(invalid));
    invalid = {};
    invalid.intensity = -1.0F;
    EXPECT_FALSE(Scene::IsLightComponentValidUVE(invalid));
    invalid = {};
    invalid.type = static_cast<Scene::LightTypeUVE>(99U);
    EXPECT_FALSE(Scene::IsLightComponentValidUVE(invalid));
    invalid = {};
    invalid.range = 0.0F;
    EXPECT_FALSE(Scene::IsLightComponentValidUVE(invalid));
    invalid = {};
    invalid.spotAngleDegrees = 180.0F;
    EXPECT_FALSE(Scene::IsLightComponentValidUVE(invalid));
    invalid = {};
    invalid.spotAngleDegrees = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsLightComponentValidUVE(invalid));
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_NoLightEntities_AllSlotsReturnIntensityZeroSentinel) {
    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    for (const LightDataUVE& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_OneDirectionalLight_PopulatesSlotZeroOnly) {
    Scene::LightComponentUVE light{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 2.0F};
    light.type = Scene::LightTypeUVE::Directional;
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{}, light));

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    EXPECT_EQ(result[0].type, Scene::LightTypeUVE::Directional);
    EXPECT_EQ(result[0].direction, (Math::Vector3UVE{0.0F, 0.0F, -1.0F}));
    EXPECT_EQ(result[0].rotation, Math::QuaternionUVE{});
    EXPECT_FLOAT_EQ(result[0].intensity, 2.0F);
    for (std::size_t i = 1; i < kMaxLightsUVE; ++i) {
        EXPECT_FLOAT_EQ(result[i].intensity, 0.0F);
    }
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_RotatedDirectionalLight_DirectionMatchesRotateVectorUVE) {
    // 180 degrees about Y: x=0, y=sin(90deg)=1, z=0, w=cos(90deg)=0 — negates x and z, so the
    // light's forward {0,0,-1} becomes {0,0,1}.
    const Math::QuaternionUVE rotation{0.0F, 1.0F, 0.0F, 0.0F};
    const Scene::LightComponentUVE light{Math::Vector3UVE{0.2F, 0.4F, 0.6F}, 3.5F};
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, rotation, light));

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    EXPECT_EQ(result[0].direction, (Math::Vector3UVE{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(result[0].rotation, rotation);
    EXPECT_EQ(result[0].color, light.color);
    EXPECT_FLOAT_EQ(result[0].intensity, light.intensity);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_OnePointLight_PositionTypeAndRangeMatch) {
    Scene::LightComponentUVE light{Math::Vector3UVE{0.5F, 0.6F, 0.7F}, 4.0F};
    light.type = Scene::LightTypeUVE::Point;
    light.range = 25.0F;
    const Math::Vector3UVE position{3.0F, 1.0F, -2.0F};
    static_cast<void>(MakeLightEntityUVE(position, Math::QuaternionUVE{}, light));

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    EXPECT_EQ(result[0].type, Scene::LightTypeUVE::Point);
    EXPECT_EQ(result[0].position, position);
    EXPECT_EQ(result[0].rotation, Math::QuaternionUVE{});
    EXPECT_FLOAT_EQ(result[0].range, 25.0F);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_OneSpotLight_PositionDirectionAndAngleMatch) {
    Scene::LightComponentUVE light{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 6.0F};
    light.type = Scene::LightTypeUVE::Spot;
    light.range = 12.0F;
    light.spotAngleDegrees = 20.0F;
    const Math::Vector3UVE position{-1.0F, 2.0F, 0.5F};
    static_cast<void>(MakeLightEntityUVE(position, Math::QuaternionUVE{}, light));

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    EXPECT_EQ(result[0].type, Scene::LightTypeUVE::Spot);
    EXPECT_EQ(result[0].position, position);
    EXPECT_EQ(result[0].direction, (Math::Vector3UVE{0.0F, 0.0F, -1.0F}));
    EXPECT_EQ(result[0].rotation, Math::QuaternionUVE{});
    EXPECT_FLOAT_EQ(result[0].range, 12.0F);
    EXPECT_FLOAT_EQ(result[0].spotAngleDegrees, 20.0F);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_LightComponentWithoutWorldTransform_SkippedNotMatched) {
    // Unlike ICameraSystemUVE (which asserts a required component is missing), ForEachUVE simply
    // never invokes the callback for an entity that doesn't match every requested component type
    // — no assert, no error, just silently excluded from the result.
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::LightComponentUVE>(
        entity, Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 5.0F});

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    for (const LightDataUVE& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_MultipleLightsSameArchetype_EachOccupiesDistinctSlotInEncounterOrder) {
    // All three entities go through the identical MakeLightEntityUVE path (Transform +
    // WorldTransform + Hierarchy + Light, nothing else) so they share one archetype — within a
    // single archetype, ForEachUVE's iteration order is chunk/row creation order, so this pins
    // "slot N == the Nth created entity" as a deterministic, testable outcome.
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                                          Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 10.0F}));
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                                          Scene::LightComponentUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 20.0F}));
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                                          Scene::LightComponentUVE{Math::Vector3UVE{0.0F, 0.0F, 1.0F}, 30.0F}));

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    EXPECT_FLOAT_EQ(result[0].intensity, 10.0F);
    EXPECT_EQ(result[0].color, (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_FLOAT_EQ(result[1].intensity, 20.0F);
    EXPECT_EQ(result[1].color, (Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    EXPECT_FLOAT_EQ(result[2].intensity, 30.0F);
    EXPECT_EQ(result[2].color, (Math::Vector3UVE{0.0F, 0.0F, 1.0F}));
    EXPECT_FLOAT_EQ(result[3].intensity, 0.0F); // unfilled sentinel
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_MoreThanMaxLights_OnlyFirstFourCreatedAreKept) {
    // kMaxLightsUVE == 4: create 6 lights, all in the same archetype, so encounter order equals
    // creation order — only the first 4 created should be kept, matching the confirmed
    // first-N-encountered policy (no distance/importance sorting).
    for (int i = 0; i < 6; ++i) {
        static_cast<void>(MakeLightEntityUVE(
            Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
            Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, static_cast<float>(i + 1)}));
    }

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    ASSERT_EQ(result.size(), kMaxLightsUVE);
    EXPECT_FLOAT_EQ(result[0].intensity, 1.0F);
    EXPECT_FLOAT_EQ(result[1].intensity, 2.0F);
    EXPECT_FLOAT_EQ(result[2].intensity, 3.0F);
    EXPECT_FLOAT_EQ(result[3].intensity, 4.0F);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_EntityWithExtraComponents_StillMatched) {
    const Scene::EntityUVE entity =
        MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                            Scene::LightComponentUVE{Math::Vector3UVE{1.0F, 1.0F, 1.0F}, 7.0F});
    entityManager.AddComponentUVE<Scene::MeshComponentUVE>(entity);

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    EXPECT_FLOAT_EQ(result[0].intensity, 7.0F);
}

#if UVE_DEBUG
TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_InvalidLightParameters_Asserts) {
    Scene::LightComponentUVE invalid = {};
    invalid.intensity = -1.0F;
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, invalid));

    EXPECT_DEATH({ static_cast<void>(lightSystem.ExtractActiveLightsUVE(entityManager)); }, "");
}
#else
TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_InvalidLightParameters_SkipsWithoutPublishing) {
    Scene::LightComponentUVE invalid = {};
    invalid.intensity = -1.0F;
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, invalid));

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    for (const LightDataUVE& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}
#endif

#if UVE_DEBUG
TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_InvalidWorldTransform_Asserts) {
    const Scene::EntityUVE entity =
        MakeLightEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, Scene::LightComponentUVE{});
    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity).worldPosition.x =
        std::numeric_limits<float>::quiet_NaN();

    EXPECT_DEATH({ static_cast<void>(lightSystem.ExtractActiveLightsUVE(entityManager)); }, "");
}
#else
TEST_F(LightSystemUVETest, ExtractActiveLightsUVE_InvalidWorldTransform_SkipsWithoutPublishing) {
    const Scene::EntityUVE entity =
        MakeLightEntityUVE(Math::Vector3UVE{}, Math::QuaternionUVE{}, Scene::LightComponentUVE{});
    entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity).worldRotation =
        Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};

    const LightListUVE result = lightSystem.ExtractActiveLightsUVE(entityManager);

    for (const LightDataUVE& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}
#endif


// ---------------------------------------------------------------------------
// View-based light selection.
//
// The bug this replaces was not a performance problem, it was a correctness one:
// with more than kMaxLightsUVE lights, the four that survived were whichever the
// ECS visited first. A torch beside the player and a lamp across the level were
// equally eligible, and the order could CHANGE when an unrelated entity was
// created or destroyed - so the light on the player's face could vanish because
// something else spawned. These tests pin the ranking, not the speed.
// ---------------------------------------------------------------------------

[[nodiscard]] Scene::LightComponentUVE MakePointLightUVE(const float intensity, const float range) {
    Scene::LightComponentUVE light;
    light.type = Scene::LightTypeUVE::Point;
    light.intensity = intensity;
    light.range = range;
    light.color = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    return light;
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_PrefersTheNearerOfTwoEqualLights) {
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{100.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(10.0F, 1000.0F)));
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{2.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(10.0F, 1000.0F)));

    const LightListUVE result =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(result[0].position.x, 2.0F);
    EXPECT_FLOAT_EQ(result[1].position.x, 100.0F);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_BrightDistantLightOutranksDimNearOne) {
    // Distance alone is the wrong metric, and this is the case that proves it: a floodlight across
    // the room legitimately matters more than a candle at arm's length.
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{2.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(0.01F, 1000.0F)));
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{20.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(10000.0F, 1000.0F)));

    const LightListUVE result =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(result[0].position.x, 20.0F);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_DirectionalLightAlwaysSurvives) {
    // The directional light is the scene's key light AND its only shadow caster. Dropping it in
    // favour of a close point light would remove every shadow in the frame at once, so it must
    // outrank any point light however near.
    for (int index = 0; index < 6; ++index) {
        static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.1F * static_cast<float>(index), 0.0F, 0.0F},
                           Math::QuaternionUVE{}, MakePointLightUVE(1000.0F, 1000.0F)));
    }
    Scene::LightComponentUVE directional;
    directional.type = Scene::LightTypeUVE::Directional;
    directional.intensity = 0.01F; // Deliberately feeble - rank must not come from intensity here.
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{}, directional));

    const LightListUVE result =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    EXPECT_EQ(result[0].type, Scene::LightTypeUVE::Directional);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_OutOfRangeLightNeverTakesASlot) {
    // A light beyond its own range contributes exactly nothing - the shader zeroes it - so it must
    // not hold a slot that a contributing light could use.
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{5.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(1000.0F, 1.0F))); // 5 units away, 1 unit range
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{50.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(1.0F, 1000.0F)));

    const LightListUVE result =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(result[0].position.x, 50.0F);
    EXPECT_FLOAT_EQ(result[1].intensity, 0.0F) << "the out-of-range light must leave the slot empty";
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_MoreLightsThanSlots_KeepsTheStrongestFour) {
    // Ten candidates, four slots: the survivors must be the four nearest, in order.
    for (int index = 0; index < 10; ++index) {
        static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{static_cast<float>(index + 1), 0.0F, 0.0F},
                           Math::QuaternionUVE{}, MakePointLightUVE(10.0F, 1000.0F)));
    }

    const LightListUVE result =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    ASSERT_EQ(result.size(), 4U);
    for (std::size_t index = 0U; index < 4U; ++index) {
        EXPECT_FLOAT_EQ(result[index].position.x, static_cast<float>(index + 1));
    }
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_IsStableWhenAnUnrelatedEntityAppears) {
    // THE regression this whole change exists for. Creating an entity that is not a light must not
    // change which lights are chosen - under first-encountered order it could.
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(10.0F, 1000.0F)));
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{50.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(10.0F, 1000.0F)));
    const LightListUVE before =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    const Scene::EntityUVE unrelated = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::MeshComponentUVE>(unrelated, Scene::MeshComponentUVE{});

    const LightListUVE after =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(before[0].position.x, after[0].position.x);
    EXPECT_FLOAT_EQ(before[1].position.x, after[1].position.x);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_TracksTheCameraAsItMoves) {
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(10.0F, 1000.0F)));
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{100.0F, 0.0F, 0.0F}, Math::QuaternionUVE{},
                       MakePointLightUVE(10.0F, 1000.0F)));

    const LightListUVE nearOrigin =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});
    const LightListUVE nearFarLight =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{100.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(nearOrigin[0].position.x, 0.0F);
    EXPECT_FLOAT_EQ(nearFarLight[0].position.x, 100.0F);
}

TEST_F(LightSystemUVETest, ExtractActiveLightsForViewUVE_RejectsExactlyWhatTheUnorderedOverloadDoes) {
    // Two overloads, one definition of validity. If these ever disagree about which lights exist,
    // switching between them would change the scene.
    Scene::LightComponentUVE valid = MakePointLightUVE(5.0F, 100.0F);
    static_cast<void>(MakeLightEntityUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::QuaternionUVE{}, valid));

    const Scene::EntityUVE noTransform = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Scene::LightComponentUVE>(noTransform, valid);

    const LightListUVE unordered = lightSystem.ExtractActiveLightsUVE(entityManager);
    const LightListUVE selected =
        lightSystem.ExtractActiveLightsForViewUVE(entityManager, Math::Vector3UVE{0.0F, 0.0F, 0.0F});

    // One light reaches both: the component without a world transform is matched by neither.
    EXPECT_FLOAT_EQ(unordered[0].intensity, 5.0F);
    EXPECT_FLOAT_EQ(selected[0].intensity, 5.0F);
    EXPECT_FLOAT_EQ(unordered[1].intensity, 0.0F);
    EXPECT_FLOAT_EQ(selected[1].intensity, 0.0F);
}

} // namespace
} // namespace UVE::Render::Tests
