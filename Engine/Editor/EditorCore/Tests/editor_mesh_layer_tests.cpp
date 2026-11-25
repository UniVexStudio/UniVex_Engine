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

#include "integration/editor_mesh_layer.h"

#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "light_emitter_component.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "color.h"
#include "memory_manager.h"
#include "directional_light_3d.h"
#include "light_3d.h"
#include "scene_graph.h"

namespace {

using namespace UVE;

class SceneLightTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;

    [[nodiscard]] Scene::Entity AddSun() {
        const Scene::Entity sun = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, sun, Scene::TransformComponent{});
        Scene::ApplyDirectionalLight3DObjectDefinition(entityManager, sun,
                                                          Scene::DirectionalLight3DObjectDefinition{});
        sceneGraph.Update(entityManager);
        return sun;
    }

    /// The viewport's own light: an internal entity carrying a plain LightComponent, exactly the
    /// shape EditorMeshLayer::CreateHeadlightEntity() builds.
    [[nodiscard]] Scene::Entity AddHeadlight(const float intensity) {
        const Scene::Entity headlight = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, headlight, Scene::TransformComponent{});
        Scene::LightComponent light{};
        light.type = Scene::LightType::Directional;
        light.intensity = intensity;
        entityManager.AddComponent<Scene::LightComponent>(headlight, light);
        entityManager.AddComponent<Scene::EditorInternalEntityComponent>(
            headlight, Scene::EditorInternalEntityComponent{});
        sceneGraph.Update(entityManager);
        return headlight;
    }

    [[nodiscard]] bool LightsItself() {
        return univex::integration::HasContributingSceneLight(entityManager);
    }

    [[nodiscard]] Scene::LightEmitterComponent& Emitter(const Scene::Entity sun) {
        return entityManager.GetComponent<Scene::LightEmitterComponent>(sun);
    }
};

TEST_F(SceneLightTest, AnEmptySceneHasNoLightOfItsOwn) {
    EXPECT_FALSE(LightsItself());
}

TEST_F(SceneLightTest, AFreshSunLightsTheScene) {
    static_cast<void>(AddSun());
    EXPECT_TRUE(LightsItself());
}

TEST_F(SceneLightTest, APlainDirectionalLight3DLightsTheScene) {
    const Scene::Entity light = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, light, Scene::TransformComponent{});
    Scene::ApplyLight3DObjectDefinition(entityManager, light, Scene::Light3DObjectDefinition{});
    sceneGraph.Update(entityManager);
    EXPECT_TRUE(LightsItself()) << "the Light3D the \"+\" menu makes is a light like any other";
}

// The four ways a light can be in the document and still light nothing. Each used to switch the
// viewport's own light off, which is the black mesh this rule exists to prevent.
TEST_F(SceneLightTest, ASunTheRendererDropsDoesNotCount) {
    const Scene::Entity sun = AddSun();
    Emitter(sun).cullMask = 0U;
    EXPECT_FALSE(LightsItself()) << "no render layers lit: the light system drops it";
}

TEST_F(SceneLightTest, ASunWithNoColourDoesNotCount) {
    const Scene::Entity sun = AddSun();
    Emitter(sun).color = Math::Color{0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(LightsItself()) << "black light adds nothing, whatever its energy says";
}

TEST_F(SceneLightTest, ASunWithNoEnergyDoesNotCount) {
    const Scene::Entity sun = AddSun();
    Emitter(sun).energy = 0.0F;
    EXPECT_FALSE(LightsItself());
}

TEST_F(SceneLightTest, AStaleInvalidSunDoesNotCount) {
    const Scene::Entity sun = AddSun();
    auto& directional = entityManager.GetComponent<Scene::DirectionalLight3DComponent>(sun);
    directional.discLimbDarkening = 4.0F; // out of range: the light system refuses the whole light
    EXPECT_FALSE(LightsItself());
}

TEST_F(SceneLightTest, TheViewportsOwnLightIsNotTheScenes) {
    static_cast<void>(AddHeadlight(1.25F));
    EXPECT_FALSE(LightsItself()) << "the fallback must not mistake itself for authored lighting";
}

// The preview sun stands down for an authored sun. It must stand down for one that lights, or the
// scene loses both suns at once and goes black.
TEST_F(SceneLightTest, ThePreviewSunOnlyYieldsToASunThatLights) {
    const auto authoredSun = [this] {
        return univex::integration::HasContributingDirectionalLight(entityManager);
    };
    EXPECT_FALSE(authoredSun());
    const Scene::Entity sun = AddSun();
    EXPECT_TRUE(authoredSun());

    Emitter(sun).cullMask = 0U;
    EXPECT_FALSE(authoredSun()) << "a sun the renderer drops must not take the preview sun with it";
    Emitter(sun).cullMask = 0xFFFFFFFFU;

    Emitter(sun).color = Math::Color{0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(authoredSun());

    Emitter(sun).color = Math::Color{1.0F, 1.0F, 1.0F};
    EXPECT_TRUE(authoredSun());
}

TEST_F(SceneLightTest, APlainLight3DIsNotASun) {
    const Scene::Entity light = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, light, Scene::TransformComponent{});
    Scene::ApplyLight3DObjectDefinition(entityManager, light, Scene::Light3DObjectDefinition{});
    sceneGraph.Update(entityManager);
    EXPECT_TRUE(LightsItself());
    EXPECT_FALSE(univex::integration::HasContributingDirectionalLight(entityManager))
        << "it lights, but it carries no sun: no disc, no cascades, nothing to stand down for";
}

TEST_F(SceneLightTest, AFixedSceneStopsCountingTheMomentItCanLight) {
    // The transition the panel lives on: authored light appears -> its own goes off; the authored
    // one is made incapable -> its own comes back rather than leaving the panel dark.
    const Scene::Entity sun = AddSun();
    EXPECT_TRUE(LightsItself());
    Emitter(sun).energy = 0.0F;
    EXPECT_FALSE(LightsItself());
    Emitter(sun).energy = 2.0F;
    EXPECT_TRUE(LightsItself());
}

TEST_F(SceneLightTest, TurningOffOrHidingAuthoredSunKeepsAuthoredNodePresentWithoutContributingLight) {
    EXPECT_FALSE(univex::integration::HasAuthoredDirectionalLightNode(entityManager));
    const Scene::Entity sun = AddSun();
    EXPECT_TRUE(univex::integration::HasAuthoredDirectionalLightNode(entityManager));
    EXPECT_TRUE(univex::integration::HasContributingDirectionalLight(entityManager));

    Emitter(sun).energy = 0.0F;
    EXPECT_TRUE(univex::integration::HasAuthoredDirectionalLightNode(entityManager))
        << "the authored DirectionalLight3D node still exists, so the preview sun must not take over";
    EXPECT_FALSE(univex::integration::HasContributingDirectionalLight(entityManager));

    Emitter(sun).energy = 1.0F;
    auto& visibility = entityManager.GetComponent<Scene::VisibilityComponent>(sun);
    visibility.visible = false;
    sceneGraph.Update(entityManager);
    EXPECT_TRUE(univex::integration::HasAuthoredDirectionalLightNode(entityManager));
    EXPECT_FALSE(univex::integration::HasContributingDirectionalLight(entityManager));
    EXPECT_FALSE(LightsItself());
}

} // namespace
