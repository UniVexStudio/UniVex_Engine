// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "light_system.h"

#include <cmath>
#include <cstddef>
#include <limits>

#include <gtest/gtest.h>

#include "event_system.h"
#include "memory_manager.h"
#include "light_component.h"
#include "mesh_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"
#include "editor_internal_entity_component.h"
#include "light_emitter_component.h"
#include "visibility_component.h"
#include "directional_light_3d.h"

namespace UVE::Render::Tests {
namespace {

class LightSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    LightSystem lightSystem;

    [[nodiscard]] Scene::Entity MakeLightEntity(Math::Vector3 position, Math::Quaternion rotation,
                                                        Scene::LightComponent light) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent local;
        local.localPosition = position;
        local.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, local);
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::LightComponent>(entity, light);
        return entity;
    }
};

TEST(LightComponentTest, IsLightComponentValid_RejectsUnsafeValues) {
    EXPECT_TRUE(Scene::IsLightComponentValid(Scene::LightComponent{}));

    Scene::LightComponent invalid = {};
    invalid.color.r = -0.1F;
    EXPECT_FALSE(Scene::IsLightComponentValid(invalid));
    invalid = {};
    invalid.color.g = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsLightComponentValid(invalid));
    invalid = {};
    invalid.intensity = -1.0F;
    EXPECT_FALSE(Scene::IsLightComponentValid(invalid));
    invalid = {};
    invalid.type = static_cast<Scene::LightType>(99U);
    EXPECT_FALSE(Scene::IsLightComponentValid(invalid));
    invalid = {};
    invalid.range = 0.0F;
    EXPECT_FALSE(Scene::IsLightComponentValid(invalid));
    invalid = {};
    invalid.spotAngleDegrees = 180.0F;
    EXPECT_FALSE(Scene::IsLightComponentValid(invalid));
    invalid = {};
    invalid.spotAngleDegrees = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Scene::IsLightComponentValid(invalid));
}

TEST_F(LightSystemTest, ExtractActiveLights_NoLightEntities_AllSlotsReturnIntensityZeroSentinel) {
    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    for (const LightData& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}

TEST_F(LightSystemTest, DirectionalLight3D_LightsTheFrameFromItsEmitterAndCarriesItsShadowSettings) {
    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition;
    definition.emitter.color = Math::Color{1.0F, 0.9F, 0.8F};
    definition.emitter.energy = 3.0F;
    definition.light.shadowMaxDistance = 40.0F;
    definition.light.shadowSplitBlend = 0.25F;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);

    const LightList lights = lightSystem.ExtractActiveLights(entityManager);
    EXPECT_EQ(lights[0].type, Scene::LightType::Directional);
    EXPECT_FLOAT_EQ(lights[0].intensity, 3.0F);
    EXPECT_FLOAT_EQ(lights[0].color.g, 0.9F);
    EXPECT_TRUE(lights[0].castsShadows);
    EXPECT_FLOAT_EQ(lights[0].shadowMaxDistance, 40.0F);
    EXPECT_FLOAT_EQ(lights[0].shadowSplitBlend, 0.25F);
    EXPECT_FLOAT_EQ(lights[0].shadowDistanceFadeRange, 10.0F);
    EXPECT_FLOAT_EQ(lights[0].shadowBias, -1.0F);
    EXPECT_FLOAT_EQ(lights[0].shadowNormalBias, -1.0F);
    EXPECT_EQ(lights.Size(), 1U) << "one light, one slot";
    // The same light is chosen when ranked for a view.
    EXPECT_FLOAT_EQ(lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{})[0].intensity, 3.0F);

    // Shadows off on the emitter: still lights, no longer casts.
    entityManager.GetComponent<Scene::LightEmitterComponent>(sun).shadowEnabled = false;
    EXPECT_FALSE(lightSystem.ExtractActiveLights(entityManager)[0].castsShadows);
    // Zero energy is the honest off switch, and the only one: a light that adds nothing is left
    // out of the list rather than occupying a slot.
    entityManager.GetComponent<Scene::LightEmitterComponent>(sun).energy = 0.0F;
    EXPECT_TRUE(lightSystem.ExtractActiveLights(entityManager).Empty());
}

TEST_F(LightSystemTest, ExtractActiveLights_OneDirectionalLight_PopulatesSlotZeroOnly) {
    Scene::LightComponent light{Math::Color{1.0F, 1.0F, 1.0F}, 2.0F};
    light.type = Scene::LightType::Directional;
    static_cast<void>(MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, light));

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    EXPECT_EQ(result[0].type, Scene::LightType::Directional);
    EXPECT_EQ(result[0].direction, (Math::Vector3{0.0F, 0.0F, -1.0F}));
    EXPECT_EQ(result[0].rotation, Math::Quaternion{});
    EXPECT_FLOAT_EQ(result[0].intensity, 2.0F);
    // One light, one slot: the list holds exactly what was extracted, and operator[] asserts
    // in bounds, so there is nothing past slot zero to read.
    EXPECT_EQ(result.Size(), 1U);
}

TEST_F(LightSystemTest, ExtractActiveLights_RotatedDirectionalLight_DirectionMatchesRotateVector) {
    // 180 degrees about Y: x=0, y=sin(90deg)=1, z=0, w=cos(90deg)=0 — negates x and z, so the
    // light's forward {0,0,-1} becomes {0,0,1}.
    const Math::Quaternion rotation{0.0F, 1.0F, 0.0F, 0.0F};
    const Scene::LightComponent light{Math::Color{0.2F, 0.4F, 0.6F}, 3.5F};
    static_cast<void>(MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, rotation, light));

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    EXPECT_EQ(result[0].direction, (Math::Vector3{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(result[0].rotation, rotation);
    EXPECT_EQ(result[0].color, light.color);
    EXPECT_FLOAT_EQ(result[0].intensity, light.intensity);
}

TEST_F(LightSystemTest, ExtractActiveLights_OnePointLight_PositionTypeAndRangeMatch) {
    Scene::LightComponent light{Math::Color{0.5F, 0.6F, 0.7F}, 4.0F};
    light.type = Scene::LightType::Point;
    light.range = 25.0F;
    const Math::Vector3 position{3.0F, 1.0F, -2.0F};
    static_cast<void>(MakeLightEntity(position, Math::Quaternion{}, light));

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    EXPECT_EQ(result[0].type, Scene::LightType::Point);
    EXPECT_EQ(result[0].position, position);
    EXPECT_EQ(result[0].rotation, Math::Quaternion{});
    EXPECT_FLOAT_EQ(result[0].range, 25.0F);
}

TEST_F(LightSystemTest, ExtractActiveLights_OneSpotLight_PositionDirectionAndAngleMatch) {
    Scene::LightComponent light{Math::Color{1.0F, 1.0F, 1.0F}, 6.0F};
    light.type = Scene::LightType::Spot;
    light.range = 12.0F;
    light.spotAngleDegrees = 20.0F;
    const Math::Vector3 position{-1.0F, 2.0F, 0.5F};
    static_cast<void>(MakeLightEntity(position, Math::Quaternion{}, light));

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    EXPECT_EQ(result[0].type, Scene::LightType::Spot);
    EXPECT_EQ(result[0].position, position);
    EXPECT_EQ(result[0].direction, (Math::Vector3{0.0F, 0.0F, -1.0F}));
    EXPECT_EQ(result[0].rotation, Math::Quaternion{});
    EXPECT_FLOAT_EQ(result[0].range, 12.0F);
    EXPECT_FLOAT_EQ(result[0].spotAngleDegrees, 20.0F);
}

TEST_F(LightSystemTest, ExtractActiveLights_LightComponentWithoutWorldTransform_SkippedNotMatched) {
    // Unlike ICameraSystem (which asserts a required component is missing), ForEach simply
    // never invokes the callback for an entity that doesn't match every requested component type
    // — no assert, no error, just silently excluded from the result.
    const Scene::Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::LightComponent>(
        entity, Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 5.0F});

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    for (const LightData& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}

TEST_F(LightSystemTest, ExtractActiveLights_MultipleLightsSameArchetype_EachOccupiesDistinctSlotInEncounterOrder) {
    // All three entities go through the identical MakeLightEntity path (Transform +
    // WorldTransform + Hierarchy + Light, nothing else) so they share one archetype — within a
    // single archetype, ForEach's iteration order is chunk/row creation order, so this pins
    // "slot N == the Nth created entity" as a deterministic, testable outcome.
    static_cast<void>(MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{},
                                          Scene::LightComponent{Math::Color{1.0F, 0.0F, 0.0F}, 10.0F}));
    static_cast<void>(MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{},
                                          Scene::LightComponent{Math::Color{0.0F, 1.0F, 0.0F}, 20.0F}));
    static_cast<void>(MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{},
                                          Scene::LightComponent{Math::Color{0.0F, 0.0F, 1.0F}, 30.0F}));

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    EXPECT_FLOAT_EQ(result[0].intensity, 10.0F);
    EXPECT_EQ(result[0].color, (Math::Color{1.0F, 0.0F, 0.0F}));
    EXPECT_FLOAT_EQ(result[1].intensity, 20.0F);
    EXPECT_EQ(result[1].color, (Math::Color{0.0F, 1.0F, 0.0F}));
    EXPECT_FLOAT_EQ(result[2].intensity, 30.0F);
    EXPECT_EQ(result[2].color, (Math::Color{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(result.Size(), 3U); // three lights, three slots, no sentinel tail
}

TEST_F(LightSystemTest, ExtractActiveLights_MoreThanMaxLights_OnlyFirstMaxCreatedAreKept) {
    // Create kMaxLights + 2 lights, all in the same archetype, so encounter order equals
    // creation order — only the first kMaxLights created should be kept, matching the confirmed
    // first-N-encountered policy (no distance/importance sorting).
    for (int i = 0; i < static_cast<int>(kMaxLights) + 2; ++i) {
        static_cast<void>(MakeLightEntity(
            Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{},
            Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, static_cast<float>(i + 1)}));
    }

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    ASSERT_EQ(result.Size(), kMaxLights);
    EXPECT_FLOAT_EQ(result[0].intensity, 1.0F);
    EXPECT_FLOAT_EQ(result[1].intensity, 2.0F);
    EXPECT_FLOAT_EQ(result[2].intensity, 3.0F);
    EXPECT_FLOAT_EQ(result[3].intensity, 4.0F);
}

TEST_F(LightSystemTest, ExtractActiveLights_EntityWithExtraComponents_StillMatched) {
    const Scene::Entity entity =
        MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{},
                            Scene::LightComponent{Math::Color{1.0F, 1.0F, 1.0F}, 7.0F});
    entityManager.AddComponent<Scene::MeshComponent>(entity);

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    EXPECT_FLOAT_EQ(result[0].intensity, 7.0F);
}

#if UVE_DEBUG
TEST_F(LightSystemTest, ExtractActiveLights_InvalidLightParameters_Asserts) {
    Scene::LightComponent invalid = {};
    invalid.intensity = -1.0F;
    static_cast<void>(MakeLightEntity(Math::Vector3{}, Math::Quaternion{}, invalid));

    EXPECT_DEATH({ static_cast<void>(lightSystem.ExtractActiveLights(entityManager)); }, "");
}
#else
TEST_F(LightSystemTest, ExtractActiveLights_InvalidLightParameters_SkipsWithoutPublishing) {
    Scene::LightComponent invalid = {};
    invalid.intensity = -1.0F;
    static_cast<void>(MakeLightEntity(Math::Vector3{}, Math::Quaternion{}, invalid));

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    for (const LightData& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}
#endif

#if UVE_DEBUG
TEST_F(LightSystemTest, ExtractActiveLights_InvalidWorldTransform_Asserts) {
    const Scene::Entity entity =
        MakeLightEntity(Math::Vector3{}, Math::Quaternion{}, Scene::LightComponent{});
    // Construct a malformed cache fixture; ordinary live cache access is read-only.
    auto invalidWorld = entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    invalidWorld.worldPosition.x = std::numeric_limits<float>::quiet_NaN();
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(entity);
    entityManager.AddComponent<Scene::WorldTransformComponent>(entity, invalidWorld);

    EXPECT_DEATH({ static_cast<void>(lightSystem.ExtractActiveLights(entityManager)); }, "");
}
#else
TEST_F(LightSystemTest, ExtractActiveLights_InvalidWorldTransform_SkipsWithoutPublishing) {
    const Scene::Entity entity =
        MakeLightEntity(Math::Vector3{}, Math::Quaternion{}, Scene::LightComponent{});
    auto invalidWorld = entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    invalidWorld.worldRotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    entityManager.RemoveComponent<Scene::WorldTransformComponent>(entity);
    entityManager.AddComponent<Scene::WorldTransformComponent>(entity, invalidWorld);

    const LightList result = lightSystem.ExtractActiveLights(entityManager);

    for (const LightData& slot : result) {
        EXPECT_FLOAT_EQ(slot.intensity, 0.0F);
    }
}
#endif


// ---------------------------------------------------------------------------
// View-based light selection.
//
// The bug this replaces was not a performance problem, it was a correctness one:
// with more than kMaxLights lights, the four that survived were whichever the
// ECS visited first. A torch beside the player and a lamp across the level were
// equally eligible, and the order could CHANGE when an unrelated entity was
// created or destroyed - so the light on the player's face could vanish because
// something else spawned. These tests pin the ranking, not the speed.
// ---------------------------------------------------------------------------

[[nodiscard]] Scene::LightComponent MakePointLight(const float intensity, const float range) {
    Scene::LightComponent light;
    light.type = Scene::LightType::Point;
    light.intensity = intensity;
    light.range = range;
    light.color = Math::Color{1.0F, 1.0F, 1.0F};
    return light;
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_PrefersTheNearerOfTwoEqualLights) {
    static_cast<void>(MakeLightEntity(Math::Vector3{100.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(10.0F, 1000.0F)));
    static_cast<void>(MakeLightEntity(Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(10.0F, 1000.0F)));

    const LightList result =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(result[0].position.x, 2.0F);
    EXPECT_FLOAT_EQ(result[1].position.x, 100.0F);
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_BrightDistantLightOutranksDimNearOne) {
    // Distance alone is the wrong metric, and this is the case that proves it: a floodlight across
    // the room legitimately matters more than a candle at arm's length.
    static_cast<void>(MakeLightEntity(Math::Vector3{2.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(0.01F, 1000.0F)));
    static_cast<void>(MakeLightEntity(Math::Vector3{20.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(10000.0F, 1000.0F)));

    const LightList result =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(result[0].position.x, 20.0F);
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_DirectionalLightAlwaysSurvives) {
    // The directional light is the scene's key light AND its only shadow caster. Dropping it in
    // favour of a close point light would remove every shadow in the frame at once, so it must
    // outrank any point light however near.
    for (int index = 0; index < 6; ++index) {
        static_cast<void>(MakeLightEntity(Math::Vector3{0.1F * static_cast<float>(index), 0.0F, 0.0F},
                           Math::Quaternion{}, MakePointLight(1000.0F, 1000.0F)));
    }
    Scene::LightComponent directional;
    directional.type = Scene::LightType::Directional;
    directional.intensity = 0.01F; // Deliberately feeble - rank must not come from intensity here.
    static_cast<void>(MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{}, directional));

    const LightList result =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_EQ(result[0].type, Scene::LightType::Directional);
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_OutOfRangeLightNeverTakesASlot) {
    // A light beyond its own range contributes exactly nothing - the shader zeroes it - so it must
    // not hold a slot that a contributing light could use.
    static_cast<void>(MakeLightEntity(Math::Vector3{5.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(1000.0F, 1.0F))); // 5 units away, 1 unit range
    static_cast<void>(MakeLightEntity(Math::Vector3{50.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(1.0F, 1000.0F)));

    const LightList result =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(result[0].position.x, 50.0F);
    EXPECT_EQ(result.Size(), 1U) << "the out-of-range light must leave no slot at all";
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_MoreLightsThanSlots_KeepsTheStrongestMax) {
    // kMaxLights + 6 candidates, kMaxLights slots: the survivors must be the nearest, in order.
    for (int index = 0; index < static_cast<int>(kMaxLights) + 6; ++index) {
        static_cast<void>(MakeLightEntity(Math::Vector3{static_cast<float>(index + 1), 0.0F, 0.0F},
                           Math::Quaternion{}, MakePointLight(10.0F, 1000.0F)));
    }

    const LightList result =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    ASSERT_EQ(result.Size(), kMaxLights);
    for (std::size_t index = 0U; index < kMaxLights; ++index) {
        EXPECT_FLOAT_EQ(result[index].position.x, static_cast<float>(index + 1));
    }
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_IsStableWhenAnUnrelatedEntityAppears) {
    // THE regression this whole change exists for. Creating an entity that is not a light must not
    // change which lights are chosen - under first-encountered order it could.
    static_cast<void>(MakeLightEntity(Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(10.0F, 1000.0F)));
    static_cast<void>(MakeLightEntity(Math::Vector3{50.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(10.0F, 1000.0F)));
    const LightList before =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    const Scene::Entity unrelated = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::MeshComponent>(unrelated, Scene::MeshComponent{});

    const LightList after =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(before[0].position.x, after[0].position.x);
    EXPECT_FLOAT_EQ(before[1].position.x, after[1].position.x);
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_TracksTheCameraAsItMoves) {
    static_cast<void>(MakeLightEntity(Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(10.0F, 1000.0F)));
    static_cast<void>(MakeLightEntity(Math::Vector3{100.0F, 0.0F, 0.0F}, Math::Quaternion{},
                       MakePointLight(10.0F, 1000.0F)));

    const LightList nearOrigin =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});
    const LightList nearFarLight =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{100.0F, 0.0F, 0.0F});

    EXPECT_FLOAT_EQ(nearOrigin[0].position.x, 0.0F);
    EXPECT_FLOAT_EQ(nearFarLight[0].position.x, 100.0F);
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_RejectsExactlyWhatTheUnorderedOverloadDoes) {
    // Two overloads, one definition of validity. If these ever disagree about which lights exist,
    // switching between them would change the scene.
    Scene::LightComponent valid = MakePointLight(5.0F, 100.0F);
    static_cast<void>(MakeLightEntity(Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Quaternion{}, valid));

    const Scene::Entity noTransform = entityManager.CreateEntity();
    entityManager.AddComponent<Scene::LightComponent>(noTransform, valid);

    const LightList unordered = lightSystem.ExtractActiveLights(entityManager);
    const LightList selected =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});

    // One light reaches both: the component without a world transform is matched by neither.
    EXPECT_FLOAT_EQ(unordered[0].intensity, 5.0F);
    EXPECT_FLOAT_EQ(selected[0].intensity, 5.0F);
    EXPECT_EQ(unordered.Size(), 1U);
    EXPECT_EQ(selected.Size(), 1U);
}

TEST_F(LightSystemTest, DirectionalLight3D_CopiesEmitterLayersSpecularShadowAndFog) {
    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition;
    definition.emitter.cullMask = 0x4U;
    definition.emitter.specular = 0.25F;
    definition.emitter.shadowOpacity = 0.5F;
    definition.emitter.shadowBlur = 2.0F;
    definition.emitter.shadowBias = 0.2F;
    definition.emitter.shadowNormalBias = 1.5F;
    definition.emitter.volumetricFogEnergy = 3.0F;
    definition.light.shadowDistanceFadeRange = 4.5F;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);

    const LightList lights = lightSystem.ExtractActiveLights(entityManager);
    EXPECT_EQ(lights[0].cullMask, 0x4U);
    EXPECT_FLOAT_EQ(lights[0].specular, 0.25F);
    EXPECT_FLOAT_EQ(lights[0].shadowOpacity, 0.5F);
    EXPECT_FLOAT_EQ(lights[0].shadowBlur, 2.0F);
    EXPECT_FLOAT_EQ(lights[0].volumetricFogEnergy, 3.0F);
    EXPECT_FLOAT_EQ(lights[0].shadowBias, 0.2F * 0.025F);
    EXPECT_FLOAT_EQ(lights[0].shadowNormalBias, 1.5F);
    EXPECT_FLOAT_EQ(lights[0].shadowDistanceFadeRange, 4.5F);
}

TEST_F(LightSystemTest, DirectionalLight3D_EmptyCullMaskDoesNotOccupyASlot) {
    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition;
    definition.emitter.cullMask = 0U;
    definition.emitter.energy = 8.0F;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);

    const LightList lights = lightSystem.ExtractActiveLights(entityManager);
    EXPECT_TRUE(lights.Empty());
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_DistanceFadeScalesEnergyThenDropsTheShadow) {
    const Scene::Entity sun = entityManager.CreateEntity();
    Scene::TransformComponent local{};
    local.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
    sceneGraph.AttachTransform(entityManager, sun, local);
    Scene::DirectionalLight3DObjectDefinition definition;
    definition.emitter.energy = 4.0F;
    definition.emitter.distanceFadeEnabled = true;
    definition.emitter.distanceFadeBegin = 40.0F;
    definition.emitter.distanceFadeShadow = 50.0F;
    definition.emitter.distanceFadeLength = 10.0F;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);

    const LightList near = lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{0.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(near[0].intensity, 4.0F);
    EXPECT_TRUE(near[0].castsShadows);

    const LightList midLight =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{45.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(midLight[0].intensity, 2.0F);
    EXPECT_TRUE(midLight[0].castsShadows);

    entityManager.GetComponent<Scene::LightEmitterComponent>(sun).distanceFadeShadow = 20.0F;
    const LightList midShadow =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{25.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(midShadow[0].intensity, 4.0F);
    EXPECT_FLOAT_EQ(midShadow[0].shadowOpacity, 0.5F);
    EXPECT_TRUE(midShadow[0].castsShadows);

    const LightList noShadow =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{30.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(noShadow[0].intensity, 4.0F);
    EXPECT_FALSE(noShadow[0].castsShadows);
}

TEST_F(LightSystemTest, ExtractActiveLightsForView_ZeroIntensityDirectionalHeadlightsNeverEvictAuthoredSun) {
    for (int i = 0; i < 4; ++i) {
        Scene::LightComponent headlight{};
        headlight.type = Scene::LightType::Directional;
        headlight.intensity = 0.0F;
        const Scene::Entity internalLight =
            MakeLightEntity(Math::Vector3{}, Math::Quaternion{}, headlight);
        entityManager.AddComponent<Scene::EditorInternalEntityComponent>(
            internalLight, Scene::EditorInternalEntityComponent{});
    }

    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition{};
    definition.emitter.energy = 2.5F;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    sceneGraph.Update(entityManager);

    const LightList selected =
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{});
    ASSERT_EQ(selected.Size(), 1U);
    EXPECT_EQ(selected[0].type, Scene::LightType::Directional);
    EXPECT_FLOAT_EQ(selected[0].intensity, 2.5F);
    EXPECT_FALSE(selected[0].isEditorInternal);
}

TEST_F(LightSystemTest, ExtractActiveLights_SkipsLightsHiddenViaVisibilityComponent) {
    const Scene::Entity sun = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
    Scene::DirectionalLight3DObjectDefinition definition{};
    definition.emitter.energy = 3.0F;
    Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun, definition);
    auto& visibility = entityManager.GetComponent<Scene::VisibilityComponent>(sun);
    visibility.visible = false;
    sceneGraph.Update(entityManager);

    EXPECT_TRUE(lightSystem.ExtractActiveLights(entityManager).Empty());
    EXPECT_TRUE(
        lightSystem.ExtractActiveLightsForView(entityManager, Math::Vector3{}).Empty());
}

} // namespace
} // namespace UVE::Render::Tests
