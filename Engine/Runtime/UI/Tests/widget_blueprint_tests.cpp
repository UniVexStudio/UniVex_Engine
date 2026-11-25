// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "blueprint_asset.h"
#include "blueprint_nodes.h"
#include "canvas_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "hierarchy_component.h"
#include "memory_manager.h"
#include "name_component.h"
#include "scene_serializer.h"
#include "transform_component.h"
#include "ui_button_component.h"
#include "ui_dropdown_component.h"
#include "ui_progress_bar_component.h"
#include "ui_text_component.h"
#include "ui_view_3d_component.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_object_host.h"
#include "widget_blueprint_asset.h"

namespace UVE::Scene::Tests {
namespace {

using Asset::BlueprintAsset;
using Asset::BlueprintNode;
using Asset::BlueprintWire;

class WidgetBlueprintTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity Make(const std::string& name, const Entity parent) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<NameComponent>(entity, NameComponent{name});
        entityManager.AddComponent<TransformComponent>(entity, TransformComponent{});
        HierarchyComponent hierarchy;
        hierarchy.parent = parent;
        entityManager.AddComponent<HierarchyComponent>(entity, hierarchy);
        return entity;
    }

    /// A Canvas with a Text "Score", a Button "Start" and a ProgressBar "Health".
    Entity MakeWidget() {
        const Entity root = Make("WBP_Hud", kInvalidEntity);
        entityManager.AddComponent<CanvasComponent>(root, CanvasComponent{});
        const Entity text = Make("Score", root);
        UITextComponent label;
        label.text = "0";
        entityManager.AddComponent<UITextComponent>(text, label);
        const Entity button = Make("Start", root);
        entityManager.AddComponent<UIButtonComponent>(button, UIButtonComponent{});
        const Entity bar = Make("Health", root);
        entityManager.AddComponent<UIProgressBarComponent>(bar, UIProgressBarComponent{});
        return root;
    }

    static BlueprintNode Node(const std::uint32_t id, const std::string& type, std::vector<std::string> values = {}) {
        BlueprintNode node;
        node.id = id;
        node.type = type;
        node.inputValues = std::move(values);
        return node;
    }

    [[nodiscard]] Blueprint::CompileResult Compile(const BlueprintAsset& graph, Core::BlueprintObjectHost& host) {
        const Asset::BlueprintTranslation translated = Asset::TranslateBlueprintToUVScript(graph, "WBP_Hud", "Canvas");
        EXPECT_TRUE(translated.problems.empty()) << translated.source;
        Blueprint::CompileResult compiled = Blueprint::CompileUVScriptSource(translated.source, host);
        for (const Blueprint::Diagnostic& d : compiled.diagnostics) {
            ADD_FAILURE() << "line " << d.at.line << ": " << d.message << "\n" << translated.source;
        }
        return compiled;
    }

    [[nodiscard]] std::string TextOf(const std::string& name) {
        std::string text;
        entityManager.ForEach<UITextComponent>([&](const Entity entity, const UITextComponent& label) {
            if (entityManager.GetComponent<NameComponent>(entity).name == name) {
                text = label.text;
            }
        });
        return text;
    }
};

TEST_F(WidgetBlueprintTest, AButtonClickRunsOnlyItsOwnEvent) {
    const Entity root = MakeWidget();
    BlueprintAsset graph;
    graph.parentKind = "Canvas";
    graph.nodes = {Node(1U, "ui_on_clicked", {"Start"}), Node(2U, "ui_set_text", {"", "Score", "Go!"}),
                   Node(3U, "ui_set_percent", {"", "Health", "0.25"})};
    graph.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{2U, 0U, 3U, 0U}};
    Core::BlueprintObjectHost host{entityManager, nullptr, root};
    const Blueprint::CompileResult compiled = Compile(graph, host);
    ASSERT_TRUE(compiled.IsSuccess());
    Blueprint::ScriptInstance instance(compiled.program, host);
    static_cast<void>(instance.RaiseEvent("ready"));

    const Blueprint::Value other[] = {std::string{"Quit"}};
    static_cast<void>(instance.RaiseEvent("ui_clicked", other));
    EXPECT_EQ(TextOf("Score"), "0") << "another button's click is not this event's";

    const Blueprint::Value start[] = {std::string{"Start"}};
    EXPECT_TRUE(instance.RaiseEvent("ui_clicked", start));
    EXPECT_EQ(TextOf("Score"), "Go!");
    float health = -1.0F;
    entityManager.ForEach<UIProgressBarComponent>(
        [&health](const Entity, const UIProgressBarComponent& bar) { health = bar.value; });
    EXPECT_FLOAT_EQ(health, 0.25F);
}

TEST_F(WidgetBlueprintTest, AWidgetReachesOnlyItsOwnWidgetsByName) {
    const Entity first = MakeWidget();
    const Entity second = MakeWidget();
    BlueprintAsset graph;
    graph.parentKind = "Canvas";
    graph.nodes = {Node(1U, "ui_on_construct"), Node(2U, "ui_set_text", {"", "Score", "mine"})};
    graph.wires = {BlueprintWire{1U, 0U, 2U, 0U}};
    Core::BlueprintObjectHost host{entityManager, nullptr, second};
    const Blueprint::CompileResult compiled = Compile(graph, host);
    ASSERT_TRUE(compiled.IsSuccess());
    Blueprint::ScriptInstance instance(compiled.program, host);
    static_cast<void>(instance.RaiseEvent("ready"));
    int mine = 0;
    int untouched = 0;
    entityManager.ForEach<UITextComponent>([&](const Entity entity, const UITextComponent& label) {
        const Entity parent = entityManager.GetComponent<HierarchyComponent>(entity).parent;
        mine += parent == second && label.text == "mine" ? 1 : 0;
        untouched += parent == first && label.text == "0" ? 1 : 0;
    });
    EXPECT_EQ(mine, 1);
    EXPECT_EQ(untouched, 1);
}

TEST_F(WidgetBlueprintTest, CreateWidgetThenAddToViewportShowsIt) {
    const Entity player = Make("Player", kInvalidEntity);
    Entity made = kInvalidEntity;
    std::string asked;
    Core::BlueprintObjectHost host{entityManager, nullptr, player};
    host.SetWidgetFactory([this, &made, &asked](const std::string& path) {
        asked = path;
        made = MakeWidget();
        entityManager.GetComponent<CanvasComponent>(made).visible = false;
        return made;
    });
    BlueprintAsset graph;
    graph.parentKind = "Object3D";
    graph.nodes = {Node(1U, "event_ready"), Node(2U, "ui_create_widget", {"", "Content/UI/WBP_Hud.uvwbp"}),
                   Node(3U, "ui_add_to_viewport", {"", "", "5"})};
    graph.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{2U, 0U, 3U, 0U}, BlueprintWire{2U, 1U, 3U, 1U}};
    const Asset::BlueprintTranslation translated = Asset::TranslateBlueprintToUVScript(graph, "BP_Player", "Object3D");
    const Blueprint::CompileResult compiled = Blueprint::CompileUVScriptSource(translated.source, host);
    ASSERT_TRUE(compiled.IsSuccess()) << translated.source;
    Blueprint::ScriptInstance instance(compiled.program, host);
    static_cast<void>(instance.RaiseEvent("ready"));
    EXPECT_EQ(asked, "Content/UI/WBP_Hud.uvwbp");
    ASSERT_NE(made, kInvalidEntity);
    const CanvasComponent& canvas = entityManager.GetComponent<CanvasComponent>(made);
    EXPECT_TRUE(canvas.visible);
    EXPECT_EQ(canvas.sortOrder, 5);
}

TEST_F(WidgetBlueprintTest, ShowMouseCursorAndAComboBoxChoiceReachTheGame) {
    const Entity root = MakeWidget();
    const Entity combo = Make("Quality", root);
    UIDropdownComponent box;
    box.options = "Low\nHigh";
    box.selectedIndex = 0;
    entityManager.AddComponent<UIDropdownComponent>(combo, box);
    std::vector<bool> shown;
    Core::BlueprintObjectHost host{entityManager, nullptr, root};
    host.SetCursorHandler([&shown](const bool show) { shown.push_back(show); });
    BlueprintAsset graph;
    graph.parentKind = "Canvas";
    graph.nodes = {Node(1U, "ui_on_construct"), Node(2U, "ui_show_mouse_cursor", {"", "true"}),
                   Node(3U, "ui_add_option", {"", "Quality", "Ultra"}), Node(4U, "ui_set_selected_index", {"", "Quality", "2"}),
                   Node(5U, "ui_on_changed", {"Quality"}), Node(6U, "ui_get_selected_option", {"Quality"}),
                   Node(7U, "ui_set_text", {"", "Score", ""})};
    graph.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{2U, 0U, 3U, 0U}, BlueprintWire{3U, 0U, 4U, 0U},
                   BlueprintWire{5U, 0U, 7U, 0U}, BlueprintWire{6U, 0U, 7U, 2U}};
    const Blueprint::CompileResult compiled = Compile(graph, host);
    ASSERT_TRUE(compiled.IsSuccess());
    Blueprint::ScriptInstance instance(compiled.program, host);
    static_cast<void>(instance.RaiseEvent("ready"));
    ASSERT_EQ(shown.size(), 1U);
    EXPECT_TRUE(shown.front());
    EXPECT_EQ(entityManager.GetComponent<UIDropdownComponent>(combo).options, "Low\nHigh\nUltra");
    EXPECT_EQ(entityManager.GetComponent<UIDropdownComponent>(combo).selectedIndex, 2);
    const Blueprint::Value changed[] = {std::string{"Quality"}};
    static_cast<void>(instance.RaiseEvent("ui_changed", changed));
    EXPECT_EQ(TextOf("Score"), "Ultra");
}

TEST_F(WidgetBlueprintTest, ShowScreenShowsOneScreenAndHidesTheOthers) {
    const Entity root = MakeWidget();
    const auto screen = [&](const std::string& name, const bool visible) {
        const Entity entity = Make(name, root);
        CanvasComponent canvas;
        canvas.visible = visible;
        entityManager.AddComponent<CanvasComponent>(entity, canvas);
        return entity;
    };
    const Entity main = screen("Main", true);
    const Entity settings = screen("Settings", false);
    const Entity button = Make("Open", main);
    entityManager.AddComponent<UIButtonComponent>(button, UIButtonComponent{});
    BlueprintAsset graph;
    graph.parentKind = "Canvas";
    graph.nodes = {Node(1U, "ui_on_clicked", {"Open"}), Node(2U, "ui_show_screen", {"", "Settings"})};
    graph.wires = {BlueprintWire{1U, 0U, 2U, 0U}};
    Core::BlueprintObjectHost host{entityManager, nullptr, root};
    const Blueprint::CompileResult compiled = Compile(graph, host);
    ASSERT_TRUE(compiled.IsSuccess());
    Blueprint::ScriptInstance instance(compiled.program, host);
    const Blueprint::Value clicked[] = {std::string{"Open"}};
    static_cast<void>(instance.RaiseEvent("ui_clicked", clicked));
    EXPECT_FALSE(entityManager.GetComponent<CanvasComponent>(main).visible);
    EXPECT_TRUE(entityManager.GetComponent<CanvasComponent>(settings).visible);
    EXPECT_TRUE(entityManager.GetComponent<CanvasComponent>(root).visible) << "the widget itself stays shown";
}

TEST(UIView3DTest, AViewIsValidOnlyWithASaneCameraAndEachStageStandsApartFarBelow) {
    UIView3DComponent view;
    EXPECT_TRUE(IsUIView3DComponentValid(view));
    view.cameraDistance = 0.0F;
    EXPECT_FALSE(IsUIView3DComponentValid(view)) << "a camera on top of its asset";
    view = UIView3DComponent{};
    view.backgroundAlpha = 2.0F;
    EXPECT_FALSE(IsUIView3DComponentValid(view));
    view = UIView3DComponent{};
    view.rect.size = Math::Vector2{2.0F, 2.0F};
    EXPECT_FALSE(IsUIView3DComponentValid(view)) << "too small to render into";

    const Math::Vector3 first = UIView3DStagePosition(0U);
    const Math::Vector3 second = UIView3DStagePosition(1U);
    EXPECT_LT(first.y, -10000.0F) << "out of every game camera's reach";
    const float dx = second.x - first.x;
    const float dz = second.z - first.z;
    EXPECT_GE(dx * dx + dz * dz, 30.0F * 30.0F) << "two stages never see each other's asset";
}

TEST_F(WidgetBlueprintTest, TheFileKeepsItsWidgetsAndItsGraph) {
    const Entity root = MakeWidget();
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "uve_widget_blueprint_test.uvwbp";
    SceneSerializer serializer;
    ASSERT_TRUE(serializer.Save(entityManager, {root}, file, SceneAssetType::Prefab));

    auto loaded = Asset::LoadWidgetBlueprintGraph(file);
    ASSERT_TRUE(loaded.has_value()) << "a widget saved without a graph has an empty one";
    EXPECT_TRUE(loaded->nodes.empty());
    EXPECT_EQ(loaded->parentKind, "Canvas");

    BlueprintAsset graph;
    graph.parentKind = "Canvas";
    graph.nodes = {Node(1U, "ui_on_clicked", {"Start"}), Node(2U, "ui_remove")};
    graph.wires = {BlueprintWire{1U, 0U, 2U, 0U}};
    ASSERT_TRUE(Asset::SaveWidgetBlueprintGraph(file, graph));
    loaded = Asset::LoadWidgetBlueprintGraph(file);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->nodes, graph.nodes);
    EXPECT_EQ(loaded->wires, graph.wires);

    // The widgets still load as an entity asset.
    const std::vector<Entity> roots = serializer.Load(entityManager, file);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_TRUE(entityManager.HasComponent<CanvasComponent>(roots.front()));
    std::filesystem::remove(file);
}

} // namespace
} // namespace UVE::Scene::Tests
