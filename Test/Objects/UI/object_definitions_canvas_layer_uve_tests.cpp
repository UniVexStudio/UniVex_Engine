// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <type_traits>

#include <gtest/gtest.h>

#include "uve/component/canvas_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/canvas_layer/all_objects_canvas_layer_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"

namespace UVE::Scene::Tests {
namespace {

// All four promoted UI kinds' definitions are reachable from the aggregate header, mirroring
// the registry test's guarantee: no CanvasLayer kind's home file can be silently dropped.
static_assert(std::is_class_v<CanvasObjectDefinitionUVE>);   // Canvas
static_assert(std::is_class_v<UITextObjectDefinitionUVE>);   // UIText
static_assert(std::is_class_v<UIImageObjectDefinitionUVE>);  // UIImage
static_assert(std::is_class_v<UIButtonObjectDefinitionUVE>); // UIButton

// The names match the Inspector's own component labels exactly, so both entry points (Add
// Component list and Add Object list) feel identical for the same kind.
static_assert(CanvasObjectDefinitionUVE::defaultName == "Canvas");
static_assert(UITextObjectDefinitionUVE::defaultName == "UI Text");
static_assert(UIImageObjectDefinitionUVE::defaultName == "UI Image");
static_assert(UIButtonObjectDefinitionUVE::defaultName == "UI Button");

class CanvasLayerObjectDefinitionsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] EntityUVE CreateEntityUVE() {
        return entityManager.CreateEntityUVE();
    }
};

TEST_F(CanvasLayerObjectDefinitionsUVETest, AllDefinitionDefaultsAreValid) {
    EXPECT_TRUE(IsCanvasObjectDefinitionValidUVE(CanvasObjectDefinitionUVE{}));
    EXPECT_TRUE(IsUITextObjectDefinitionValidUVE(UITextObjectDefinitionUVE{}));
    EXPECT_TRUE(IsUIImageObjectDefinitionValidUVE(UIImageObjectDefinitionUVE{}));
    EXPECT_TRUE(IsUIButtonObjectDefinitionValidUVE(UIButtonObjectDefinitionUVE{}));
}

TEST_F(CanvasLayerObjectDefinitionsUVETest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCanvasObjectDefinitionUVE(entityManager, entity, CanvasObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<CanvasComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUITextObjectDefinitionUVE(entityManager, entity, UITextObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UITextComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUIImageObjectDefinitionUVE(entityManager, entity, UIImageObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UIImageComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUIButtonObjectDefinitionUVE(entityManager, entity, UIButtonObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UIButtonComponentUVE>(entity));
    }
}

TEST_F(CanvasLayerObjectDefinitionsUVETest, PromotedKindsAreRegisteredAndLibraryCreatable) {
    // The whole point of the promotion: these kinds must now appear in the Add-Object list's
    // backing registry, creatable like every other library kind.
    for (const std::string_view typeId : {"canvas", "ui_text", "ui_image", "ui_button"}) {
        const Objects::SceneObjectDescriptorUVE* descriptor = Objects::FindSceneObjectDescriptorUVE(typeId);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->category, "UI");
        EXPECT_TRUE(descriptor->libraryCreatable);
    }
}

} // namespace
} // namespace UVE::Scene::Tests
