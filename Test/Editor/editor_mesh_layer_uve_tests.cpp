// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// What counts as "the scene lights itself".
//
// The Viewport panel runs one light of its own so that the first object an author places is not a
// black shape - but only while the document has no light of its own. That rule used to be a guess
// about the document ("is there a light with energy?"), and a light the renderer drops - because
// its cull mask is empty, its colour is black, its energy is zero, or it is a stale invalid
// component - still satisfied the guess. The panel then switched its own light off for a scene
// nothing lit: a black mesh with nothing on screen to explain it. These cases pin the rule to the
// renderer's own answer instead.

#include <gtest/gtest.h>

#include "integration/EditorMeshLayer.h"

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/light_emitter_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/color_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/directional_light_3d_uve.h"
#include "uve/objects/3d/light_3d_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace {

using namespace UVE;

class SceneLightUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;

    [[nodiscard]] Scene::EntityUVE AddSunUVE() {
        const Scene::EntityUVE sun = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, sun, Scene::TransformComponentUVE{});
        Scene::ApplyDirectionalLight3DObjectDefinitionUVE(entityManager, sun,
                                                          Scene::DirectionalLight3DObjectDefinitionUVE{});
        sceneGraph.UpdateUVE(entityManager);
        return sun;
    }

    /// The viewport's own light: an internal entity carrying a plain LightComponent, exactly the
    /// shape EditorMeshLayerUVE::CreateHeadlightEntityUVE() builds.
    [[nodiscard]] Scene::EntityUVE AddHeadlightUVE(const float intensity) {
        const Scene::EntityUVE headlight = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, headlight, Scene::TransformComponentUVE{});
        Scene::LightComponentUVE light{};
        light.type = Scene::LightTypeUVE::Directional;
        light.intensity = intensity;
        entityManager.AddComponentUVE<Scene::LightComponentUVE>(headlight, light);
        entityManager.AddComponentUVE<Scene::EditorInternalEntityComponentUVE>(
            headlight, Scene::EditorInternalEntityComponentUVE{});
        sceneGraph.UpdateUVE(entityManager);
        return headlight;
    }

    [[nodiscard]] bool LightsItselfUVE() {
        return univex::integration::HasContributingSceneLightUVE(entityManager);
    }

    [[nodiscard]] Scene::LightEmitterComponentUVE& EmitterUVE(const Scene::EntityUVE sun) {
        return entityManager.GetComponentUVE<Scene::LightEmitterComponentUVE>(sun);
    }
};

TEST_F(SceneLightUVETest, AnEmptySceneHasNoLightOfItsOwn) {
    EXPECT_FALSE(LightsItselfUVE());
}

TEST_F(SceneLightUVETest, AFreshSunLightsTheScene) {
    static_cast<void>(AddSunUVE());
    EXPECT_TRUE(LightsItselfUVE());
}

TEST_F(SceneLightUVETest, APlainDirectionalLight3DLightsTheScene) {
    const Scene::EntityUVE light = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, light, Scene::TransformComponentUVE{});
    Scene::ApplyLight3DObjectDefinitionUVE(entityManager, light, Scene::Light3DObjectDefinitionUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_TRUE(LightsItselfUVE()) << "the Light3D the \"+\" menu makes is a light like any other";
}

// The four ways a light can be in the document and still light nothing. Each used to switch the
// viewport's own light off, which is the black mesh this rule exists to prevent.
TEST_F(SceneLightUVETest, ASunTheRendererDropsDoesNotCount) {
    const Scene::EntityUVE sun = AddSunUVE();
    EmitterUVE(sun).cullMask = 0U;
    EXPECT_FALSE(LightsItselfUVE()) << "no render layers lit: the light system drops it";
}

TEST_F(SceneLightUVETest, ASunWithNoColourDoesNotCount) {
    const Scene::EntityUVE sun = AddSunUVE();
    EmitterUVE(sun).color = Math::ColorUVE{0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(LightsItselfUVE()) << "black light adds nothing, whatever its energy says";
}

TEST_F(SceneLightUVETest, ASunWithNoEnergyDoesNotCount) {
    const Scene::EntityUVE sun = AddSunUVE();
    EmitterUVE(sun).energy = 0.0F;
    EXPECT_FALSE(LightsItselfUVE());
}

TEST_F(SceneLightUVETest, AStaleInvalidSunDoesNotCount) {
    const Scene::EntityUVE sun = AddSunUVE();
    auto& directional = entityManager.GetComponentUVE<Scene::DirectionalLight3DComponentUVE>(sun);
    directional.discLimbDarkening = 4.0F; // out of range: the light system refuses the whole light
    EXPECT_FALSE(LightsItselfUVE());
}

TEST_F(SceneLightUVETest, TheViewportsOwnLightIsNotTheScenes) {
    static_cast<void>(AddHeadlightUVE(1.25F));
    EXPECT_FALSE(LightsItselfUVE()) << "the fallback must not mistake itself for authored lighting";
}

// The preview sun stands down for an authored sun. It must stand down for one that lights, or the
// scene loses both suns at once and goes black.
TEST_F(SceneLightUVETest, ThePreviewSunOnlyYieldsToASunThatLights) {
    const auto authoredSun = [this] {
        return univex::integration::HasContributingDirectionalLightUVE(entityManager);
    };
    EXPECT_FALSE(authoredSun());
    const Scene::EntityUVE sun = AddSunUVE();
    EXPECT_TRUE(authoredSun());

    EmitterUVE(sun).cullMask = 0U;
    EXPECT_FALSE(authoredSun()) << "a sun the renderer drops must not take the preview sun with it";
    EmitterUVE(sun).cullMask = 0xFFFFFFFFU;

    EmitterUVE(sun).color = Math::ColorUVE{0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(authoredSun());

    EmitterUVE(sun).color = Math::ColorUVE{1.0F, 1.0F, 1.0F};
    EXPECT_TRUE(authoredSun());
}

TEST_F(SceneLightUVETest, APlainLight3DIsNotASun) {
    const Scene::EntityUVE light = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, light, Scene::TransformComponentUVE{});
    Scene::ApplyLight3DObjectDefinitionUVE(entityManager, light, Scene::Light3DObjectDefinitionUVE{});
    sceneGraph.UpdateUVE(entityManager);
    EXPECT_TRUE(LightsItselfUVE());
    EXPECT_FALSE(univex::integration::HasContributingDirectionalLightUVE(entityManager))
        << "it lights, but it carries no sun: no disc, no cascades, nothing to stand down for";
}

TEST_F(SceneLightUVETest, AFixedSceneStopsCountingTheMomentItCanLight) {
    // The transition the panel lives on: authored light appears -> its own goes off; the authored
    // one is made incapable -> its own comes back rather than leaving the panel dark.
    const Scene::EntityUVE sun = AddSunUVE();
    EXPECT_TRUE(LightsItselfUVE());
    EmitterUVE(sun).energy = 0.0F;
    EXPECT_FALSE(LightsItselfUVE());
    EmitterUVE(sun).energy = 2.0F;
    EXPECT_TRUE(LightsItselfUVE());
}

} // namespace
