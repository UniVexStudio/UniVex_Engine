// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "animation_graph_component.h"
#include "audio_source_component.h"
#include "blueprint_asset.h"
#include "blueprint_nodes.h"
#include "character_controller_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "memory_manager.h"
#include "montage_player_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "uvscript_compiler.h"
#include "uvscript_instance.h"
#include "uvscript_object_host.h"

namespace UVE::Scene::Tests {
namespace {

using Asset::BlueprintAssetUVE;
using Asset::BlueprintNodeRoleUVE;
using Asset::BlueprintNodeUVE;
using Asset::BlueprintPinTypeUVE;
using Asset::BlueprintWireUVE;

class BlueprintUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    /// An object with every surface a node can reach: a body, a character, a sound, a montage
    /// player, gameplay pools and tags.
    EntityUVE MakeEverythingUVE() {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<TransformComponentUVE>(entity, TransformComponentUVE{});
        entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, Rigid3DComponentUVE{});
        entityManager.AddComponentUVE<CharacterControllerComponentUVE>(entity, CharacterControllerComponentUVE{});
        entityManager.AddComponentUVE<AudioSourceComponentUVE>(entity, AudioSourceComponentUVE{});
        entityManager.AddComponentUVE<MontagePlayerComponentUVE>(entity, MontagePlayerComponentUVE{});
        AnimationGraphComponentUVE graph;
        graph.parameters.push_back(AnimationParameterUVE{"Jump", AnimationParameterTypeUVE::Trigger});
        graph.parameters.push_back(AnimationParameterUVE{"IsJumping", AnimationParameterTypeUVE::Bool});
        AnimationParameterUVE speed{"Speed", AnimationParameterTypeUVE::Float};
        speed.source = AnimationParameterSourceUVE::CharacterSpeed;
        graph.parameters.push_back(speed);
        entityManager.AddComponentUVE<AnimationGraphComponentUVE>(entity, graph);
        GameplayAttributesComponentUVE pools;
        EXPECT_TRUE(AddGameplayAttributeUVE(pools, "stamina", 10.0F, 10.0F));
        entityManager.AddComponentUVE<GameplayAttributesComponentUVE>(entity, pools);
        entityManager.AddComponentUVE<GameplayTagComponentUVE>(entity, GameplayTagComponentUVE{});
        return entity;
    }

    /// Translates and compiles; every diagnostic fails the test with the source around it.
    void ExpectCompilesUVE(const BlueprintAssetUVE& blueprint, const std::string& what) {
        const EntityUVE entity = MakeEverythingUVE();
        Core::UVScriptObjectHostUVE host{entityManager, nullptr, entity};
        const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP_Test", "Character3D");
        for (const auto& [node, problem] : translated.problems) {
            ADD_FAILURE() << what << ": node " << node << ": " << problem;
        }
        const UVScript::CompileResultUVE result = UVScript::CompileUVScriptSourceUVE(translated.source, host);
        for (const UVScript::DiagnosticUVE& d : result.diagnostics) {
            ADD_FAILURE() << what << " line " << d.at.line << ": " << d.message << "\n" << translated.source;
        }
    }

    static BlueprintNodeUVE NodeUVE(const std::uint32_t id, const std::string& type, const std::string& variable = "") {
        BlueprintNodeUVE node;
        node.id = id;
        node.type = type;
        node.variable = variable;
        return node;
    }
};

TEST_F(BlueprintUVETest, EveryNodeOfThePaletteTranslatesToUVScriptThatCompiles) {
    for (const Asset::BlueprintNodeTypeUVE& type : Asset::GetBlueprintNodeTypesUVE()) {
        BlueprintAssetUVE blueprint;
        if (type.role == BlueprintNodeRoleUVE::Event) {
            blueprint.nodes = {NodeUVE(1U, type.key)};
            ExpectCompilesUVE(blueprint, type.key);
            continue;
        }
        if (type.role == BlueprintNodeRoleUVE::GetVariable || type.role == BlueprintNodeRoleUVE::SetVariable) {
            continue;   // the next test
        }
        blueprint.nodes.push_back(NodeUVE(1U, "event_tick"));
        const bool needsObject = type.key == "hide_object" || type.key == "show_object" || type.key == "is_valid" ||
                                 type.key == "ui_add_to_viewport";
        if (type.role == BlueprintNodeRoleUVE::Pure) {
            // Its first output stored in a variable of that type.
            blueprint.variables.push_back({"result", type.outputs.front().type, "", false});
            blueprint.nodes.push_back(NodeUVE(2U, type.key));
            blueprint.nodes.push_back(NodeUVE(3U, "set_variable", "result"));
            blueprint.wires = {BlueprintWireUVE{1U, 0U, 3U, 0U}, BlueprintWireUVE{2U, 0U, 3U, 1U}};
        } else {
            blueprint.nodes.push_back(NodeUVE(2U, type.key));
            blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}};
            if (needsObject) {
                BlueprintNodeUVE find = NodeUVE(3U, "find_object");
                find.inputValues = {"Door"};
                blueprint.nodes.push_back(find);
                blueprint.wires.push_back(BlueprintWireUVE{3U, 0U, 2U, 1U});
            }
        }
        ExpectCompilesUVE(blueprint, type.key);
    }
}

TEST_F(BlueprintUVETest, VariablesBecomeFieldsAndExposedOnesExports) {
    BlueprintAssetUVE blueprint;
    blueprint.variables = {{"speed", BlueprintPinTypeUVE::Float, "6", true},
                           {"jumps", BlueprintPinTypeUVE::Int, "0", false},
                           {"offset", BlueprintPinTypeUVE::Vector, "0,1,0", true},
                           {"label", BlueprintPinTypeUVE::String, "hi", false},
                           {"armed", BlueprintPinTypeUVE::Bool, "true", false}};
    blueprint.nodes = {NodeUVE(1U, "event_ready"), NodeUVE(2U, "set_variable", "jumps"), NodeUVE(3U, "get_variable", "jumps"),
                       NodeUVE(4U, "int_add")};
    blueprint.nodes[3].inputValues = {"", "1"};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{3U, 0U, 4U, 0U}, BlueprintWireUVE{4U, 0U, 2U, 1U}};
    const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP_Player", "Player3D");
    EXPECT_NE(translated.source.find("entity BP_Player : Player3D"), std::string::npos);
    EXPECT_NE(translated.source.find("export speed: float = 6.0"), std::string::npos);
    EXPECT_NE(translated.source.find("var jumps: int = 0"), std::string::npos);
    EXPECT_NE(translated.source.find("jumps = ((jumps + 1))"), std::string::npos) << translated.source;
    ExpectCompilesUVE(blueprint, "variables");
}

TEST_F(BlueprintUVETest, FlowNodesNestTheirChains) {
    BlueprintAssetUVE blueprint;
    blueprint.nodes = {NodeUVE(1U, "event_tick"), NodeUVE(2U, "branch"), NodeUVE(3U, "print"), NodeUVE(4U, "for_loop"), NodeUVE(5U, "print"),
                       NodeUVE(6U, "input_pressed")};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{6U, 0U, 2U, 1U}, BlueprintWireUVE{2U, 0U, 3U, 0U},
                       BlueprintWireUVE{2U, 1U, 4U, 0U}, BlueprintWireUVE{4U, 0U, 5U, 0U}};
    const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Object3D");
    EXPECT_NE(translated.source.find("if (input.pressed(\"jump\")):"), std::string::npos) << translated.source;
    EXPECT_NE(translated.source.find("else:"), std::string::npos);
    EXPECT_NE(translated.source.find("for i4 in 0..(9 + 1):"), std::string::npos) << "Last Index is inclusive";
    // The print inside the loop is attributed to its node.
    bool found = false;
    for (std::uint32_t line = 1U; line <= translated.lineNodes.size(); ++line) {
        found = found || translated.NodeAtLineUVE(line) == 5U;
    }
    EXPECT_TRUE(found);
    ExpectCompilesUVE(blueprint, "flow");
}

TEST_F(BlueprintUVETest, AReadyBlueprintRunsAndMovesItsObject) {
    BlueprintAssetUVE blueprint;
    BlueprintNodeUVE set = NodeUVE(2U, "set_position");
    set.inputValues = {"", "1,2,3"};
    blueprint.nodes = {NodeUVE(1U, "event_ready"), set};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}};
    const EntityUVE entity = MakeEverythingUVE();
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entity};
    const UVScript::CompileResultUVE compiled =
        UVScript::CompileUVScriptSourceUVE(Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Object3D").source, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    UVScript::ScriptInstanceUVE instance(compiled.program, host);
    static_cast<void>(instance.RaiseEventUVE("ready", {}));
    const TransformComponentUVE& transform = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
    EXPECT_FLOAT_EQ(transform.localPosition.x, 1.0F);
    EXPECT_FLOAT_EQ(transform.localPosition.z, 3.0F);
}

TEST_F(BlueprintUVETest, AFileRoundTripsAndABadOneIsRefused) {
    BlueprintAssetUVE blueprint;
    blueprint.parentKind = "Player3D";
    blueprint.variables = {{"speed", BlueprintPinTypeUVE::Float, "6", true}};
    blueprint.nodes = {NodeUVE(1U, "event_tick"), NodeUVE(2U, "print")};
    blueprint.nodes[1].inputValues = {"", "tick"};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}};
    blueprint.comments.push_back(Asset::BlueprintCommentUVE{"Movement", 10.0F, 20.0F, 300.0F, 200.0F, 0x112233FFU});
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "uve_blueprint_round_trip_test.uvbp";
    ASSERT_TRUE(Asset::SaveBlueprintAssetUVE(blueprint, file));
    BlueprintAssetUVE read;
    ASSERT_TRUE(Asset::LoadBlueprintAssetUVE(file, read));
    EXPECT_EQ(read, blueprint);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);

    BlueprintAssetUVE bad = blueprint;
    bad.wires.push_back(BlueprintWireUVE{1U, 0U, 99U, 0U});
    EXPECT_FALSE(Asset::IsBlueprintAssetValidUVE(bad)) << "a wire to no node";
    bad = blueprint;
    bad.variables.push_back(bad.variables.front());
    EXPECT_FALSE(Asset::IsBlueprintAssetValidUVE(bad)) << "two variables with one name";
}

TEST_F(BlueprintUVETest, BlueprintsSetAnimationGraphParameters) {
    const EntityUVE entity = MakeEverythingUVE();
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entity};
    const auto call = [&](const std::string_view name, std::vector<UVScript::ValueUVE> args) {
        return host.CallFunctionUVE(name, args);
    };
    const auto& graph = entityManager.GetComponentUVE<AnimationGraphComponentUVE>(entity);
    EXPECT_EQ(call("anim.set_bool", {std::string("IsJumping"), true}), UVScript::ValueUVE{true});
    EXPECT_EQ(graph.parameters[1].value, 1.0F);
    EXPECT_EQ(call("anim.get_bool", {std::string("IsJumping")}), UVScript::ValueUVE{true});
    EXPECT_EQ(call("anim.trigger", {std::string("Jump")}), UVScript::ValueUVE{true});
    EXPECT_EQ(graph.parameters[0].value, 1.0F);
    // The character writes Speed every frame: a script may read it, not set it.
    EXPECT_EQ(call("anim.set_float", {std::string("Speed"), 3.0}), UVScript::ValueUVE{false});
    EXPECT_EQ(call("anim.set_float", {std::string("Missing"), 3.0}), UVScript::ValueUVE{false});
}

} // namespace
} // namespace UVE::Scene::Tests
