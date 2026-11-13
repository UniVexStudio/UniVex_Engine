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
#include "ui_progress_bar_component.h"
#include "ui_text_component.h"
#include "uvscript_compiler.h"
#include "uvscript_instance.h"
#include "uvscript_object_host.h"
#include "widget_blueprint_asset.h"

namespace UVE::Scene::Tests {
namespace {

using Asset::BlueprintAssetUVE;
using Asset::BlueprintNodeUVE;
using Asset::BlueprintWireUVE;

class WidgetBlueprintUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakeUVE(const std::string& name, const EntityUVE parent) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{name});
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
        HierarchyComponentUVE hierarchy;
        hierarchy.parent = parent;
        entityManager.AddComponentUVE<HierarchyComponentUVE>(entity, hierarchy);
        return entity;
    }

    /// A Canvas with a Text "Score", a Button "Start" and a ProgressBar "Health".
    EntityUVE MakeWidgetUVE() {
        const EntityUVE root = MakeUVE("WBP_Hud", kInvalidEntityUVE);
        entityManager.AddComponentUVE<CanvasComponentUVE>(root, CanvasComponentUVE{});
        const EntityUVE text = MakeUVE("Score", root);
        UITextComponentUVE label;
        label.text = "0";
        entityManager.AddComponentUVE<UITextComponentUVE>(text, label);
        const EntityUVE button = MakeUVE("Start", root);
        entityManager.AddComponentUVE<UIButtonComponentUVE>(button, UIButtonComponentUVE{});
        const EntityUVE bar = MakeUVE("Health", root);
        entityManager.AddComponentUVE<UIProgressBarComponentUVE>(bar, UIProgressBarComponentUVE{});
        return root;
    }

    static BlueprintNodeUVE NodeUVE(const std::uint32_t id, const std::string& type, std::vector<std::string> values = {}) {
        BlueprintNodeUVE node;
        node.id = id;
        node.type = type;
        node.inputValues = std::move(values);
        return node;
    }

    [[nodiscard]] UVScript::CompileResultUVE CompileUVE(const BlueprintAssetUVE& graph, Core::UVScriptObjectHostUVE& host) {
        const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(graph, "WBP_Hud", "Canvas");
        EXPECT_TRUE(translated.problems.empty()) << translated.source;
        UVScript::CompileResultUVE compiled = UVScript::CompileUVScriptSourceUVE(translated.source, host);
        for (const UVScript::DiagnosticUVE& d : compiled.diagnostics) {
            ADD_FAILURE() << "line " << d.at.line << ": " << d.message << "\n" << translated.source;
        }
        return compiled;
    }

    [[nodiscard]] std::string TextOfUVE(const std::string& name) {
        std::string text;
        entityManager.ForEachUVE<UITextComponentUVE>([&](const EntityUVE entity, const UITextComponentUVE& label) {
            if (entityManager.GetComponentUVE<NameComponentUVE>(entity).name == name) {
                text = label.text;
            }
        });
        return text;
    }
};

TEST_F(WidgetBlueprintUVETest, AButtonClickRunsOnlyItsOwnEvent) {
    const EntityUVE root = MakeWidgetUVE();
    BlueprintAssetUVE graph;
    graph.parentKind = "Canvas";
    graph.nodes = {NodeUVE(1U, "ui_on_clicked", {"Start"}), NodeUVE(2U, "ui_set_text", {"", "Score", "Go!"}),
                   NodeUVE(3U, "ui_set_percent", {"", "Health", "0.25"})};
    graph.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{2U, 0U, 3U, 0U}};
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, root};
    const UVScript::CompileResultUVE compiled = CompileUVE(graph, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    UVScript::ScriptInstanceUVE instance(compiled.program, host);
    static_cast<void>(instance.RaiseEventUVE("ready"));

    const UVScript::ValueUVE other[] = {std::string{"Quit"}};
    static_cast<void>(instance.RaiseEventUVE("ui_clicked", other));
    EXPECT_EQ(TextOfUVE("Score"), "0") << "another button's click is not this event's";

    const UVScript::ValueUVE start[] = {std::string{"Start"}};
    EXPECT_TRUE(instance.RaiseEventUVE("ui_clicked", start));
    EXPECT_EQ(TextOfUVE("Score"), "Go!");
    float health = -1.0F;
    entityManager.ForEachUVE<UIProgressBarComponentUVE>(
        [&health](const EntityUVE, const UIProgressBarComponentUVE& bar) { health = bar.value; });
    EXPECT_FLOAT_EQ(health, 0.25F);
}

TEST_F(WidgetBlueprintUVETest, AWidgetReachesOnlyItsOwnWidgetsByName) {
    const EntityUVE first = MakeWidgetUVE();
    const EntityUVE second = MakeWidgetUVE();
    BlueprintAssetUVE graph;
    graph.parentKind = "Canvas";
    graph.nodes = {NodeUVE(1U, "ui_on_construct"), NodeUVE(2U, "ui_set_text", {"", "Score", "mine"})};
    graph.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}};
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, second};
    const UVScript::CompileResultUVE compiled = CompileUVE(graph, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    UVScript::ScriptInstanceUVE instance(compiled.program, host);
    static_cast<void>(instance.RaiseEventUVE("ready"));
    int mine = 0;
    int untouched = 0;
    entityManager.ForEachUVE<UITextComponentUVE>([&](const EntityUVE entity, const UITextComponentUVE& label) {
        const EntityUVE parent = entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent;
        mine += parent == second && label.text == "mine" ? 1 : 0;
        untouched += parent == first && label.text == "0" ? 1 : 0;
    });
    EXPECT_EQ(mine, 1);
    EXPECT_EQ(untouched, 1);
}

TEST_F(WidgetBlueprintUVETest, CreateWidgetThenAddToViewportShowsIt) {
    const EntityUVE player = MakeUVE("Player", kInvalidEntityUVE);
    EntityUVE made = kInvalidEntityUVE;
    std::string asked;
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, player};
    host.SetWidgetFactoryUVE([this, &made, &asked](const std::string& path) {
        asked = path;
        made = MakeWidgetUVE();
        entityManager.GetComponentUVE<CanvasComponentUVE>(made).visible = false;
        return made;
    });
    BlueprintAssetUVE graph;
    graph.parentKind = "Object3D";
    graph.nodes = {NodeUVE(1U, "event_ready"), NodeUVE(2U, "ui_create_widget", {"", "Content/UI/WBP_Hud.uvwbp"}),
                   NodeUVE(3U, "ui_add_to_viewport", {"", "", "5"})};
    graph.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{2U, 0U, 3U, 0U}, BlueprintWireUVE{2U, 1U, 3U, 1U}};
    const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(graph, "BP_Player", "Object3D");
    const UVScript::CompileResultUVE compiled = UVScript::CompileUVScriptSourceUVE(translated.source, host);
    ASSERT_TRUE(compiled.IsSuccessUVE()) << translated.source;
    UVScript::ScriptInstanceUVE instance(compiled.program, host);
    static_cast<void>(instance.RaiseEventUVE("ready"));
    EXPECT_EQ(asked, "Content/UI/WBP_Hud.uvwbp");
    ASSERT_NE(made, kInvalidEntityUVE);
    const CanvasComponentUVE& canvas = entityManager.GetComponentUVE<CanvasComponentUVE>(made);
    EXPECT_TRUE(canvas.visible);
    EXPECT_EQ(canvas.sortOrder, 5);
}

TEST_F(WidgetBlueprintUVETest, TheFileKeepsItsWidgetsAndItsGraph) {
    const EntityUVE root = MakeWidgetUVE();
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "uve_widget_blueprint_test.uvwbp";
    SceneSerializerUVE serializer;
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {root}, file, SceneAssetTypeUVE::Prefab));

    auto loaded = Asset::LoadWidgetBlueprintGraphUVE(file);
    ASSERT_TRUE(loaded.has_value()) << "a widget saved without a graph has an empty one";
    EXPECT_TRUE(loaded->nodes.empty());
    EXPECT_EQ(loaded->parentKind, "Canvas");

    BlueprintAssetUVE graph;
    graph.parentKind = "Canvas";
    graph.nodes = {NodeUVE(1U, "ui_on_clicked", {"Start"}), NodeUVE(2U, "ui_remove")};
    graph.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}};
    ASSERT_TRUE(Asset::SaveWidgetBlueprintGraphUVE(file, graph));
    loaded = Asset::LoadWidgetBlueprintGraphUVE(file);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->nodes, graph.nodes);
    EXPECT_EQ(loaded->wires, graph.wires);

    // The widgets still load as an entity asset.
    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, file);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_TRUE(entityManager.HasComponentUVE<CanvasComponentUVE>(roots.front()));
    std::filesystem::remove(file);
}

} // namespace
} // namespace UVE::Scene::Tests
