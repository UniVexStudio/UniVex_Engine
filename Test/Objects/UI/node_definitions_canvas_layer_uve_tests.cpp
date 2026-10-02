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
#include "uve/nodes/canvas_layer/all_nodes_canvas_layer_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"

namespace UVE::Scene::Tests {
namespace {

// All four promoted UI kinds' definitions are reachable from the aggregate header, mirroring
// the registry test's guarantee: no CanvasLayer kind's home file can be silently dropped.
static_assert(std::is_class_v<CanvasNodeDefinitionUVE>);   // Canvas
static_assert(std::is_class_v<UITextNodeDefinitionUVE>);   // UIText
static_assert(std::is_class_v<UIImageNodeDefinitionUVE>);  // UIImage
static_assert(std::is_class_v<UIButtonNodeDefinitionUVE>); // UIButton

// The names match the Inspector's own component labels exactly, so both entry points (Add
// Component list and Add Node list) feel identical for the same kind.
static_assert(CanvasNodeDefinitionUVE::defaultName == "Canvas");
static_assert(UITextNodeDefinitionUVE::defaultName == "UI Text");
static_assert(UIImageNodeDefinitionUVE::defaultName == "UI Image");
static_assert(UIButtonNodeDefinitionUVE::defaultName == "UI Button");

class CanvasLayerNodeDefinitionsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] EntityUVE CreateEntityUVE() {
        return entityManager.CreateEntityUVE();
    }
};

TEST_F(CanvasLayerNodeDefinitionsUVETest, AllDefinitionDefaultsAreValid) {
    EXPECT_TRUE(IsCanvasNodeDefinitionValidUVE(CanvasNodeDefinitionUVE{}));
    EXPECT_TRUE(IsUITextNodeDefinitionValidUVE(UITextNodeDefinitionUVE{}));
    EXPECT_TRUE(IsUIImageNodeDefinitionValidUVE(UIImageNodeDefinitionUVE{}));
    EXPECT_TRUE(IsUIButtonNodeDefinitionValidUVE(UIButtonNodeDefinitionUVE{}));
}

TEST_F(CanvasLayerNodeDefinitionsUVETest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCanvasNodeDefinitionUVE(entityManager, entity, CanvasNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<CanvasComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUITextNodeDefinitionUVE(entityManager, entity, UITextNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UITextComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUIImageNodeDefinitionUVE(entityManager, entity, UIImageNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UIImageComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUIButtonNodeDefinitionUVE(entityManager, entity, UIButtonNodeDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UIButtonComponentUVE>(entity));
    }
}

TEST_F(CanvasLayerNodeDefinitionsUVETest, PromotedKindsAreRegisteredAndLibraryCreatable) {
    // The whole point of the promotion: these kinds must now appear in the Add-Node list's
    // backing registry, creatable like every other library kind.
    for (const std::string_view typeId : {"canvas", "ui_text", "ui_image", "ui_button"}) {
        const Nodes::SceneNodeDescriptorUVE* descriptor = Nodes::FindSceneNodeDescriptorUVE(typeId);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->category, "UI");
        EXPECT_TRUE(descriptor->libraryCreatable);
    }
}

} // namespace
} // namespace UVE::Scene::Tests
