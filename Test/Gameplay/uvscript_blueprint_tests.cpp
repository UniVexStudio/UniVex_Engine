// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "camera_component.h"
#include "light_component.h"
#include "hierarchy_component.h"
#include "name_component.h"

#include "animation_graph_component.h"
#include "audio_source_component.h"
#include "blueprint_asset.h"
#include "blueprint_nodes.h"
#include "character_controller_component.h"
#include "character_3d.h"
#include "quaternion.h"
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
        entityManager.AddComponentUVE<LightComponentUVE>(entity, LightComponentUVE{});
        entityManager.AddComponentUVE<CameraComponentUVE>(entity, CameraComponentUVE{});
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

    /// A graph made to run: its host (with `services`), its program and a running instance.
    struct RunningUVE final {
        std::unique_ptr<Core::UVScriptObjectHostUVE> host;
        std::unique_ptr<UVScript::ScriptInstanceUVE> instance;
        [[nodiscard]] UVScript::ValueUVE FieldUVE(const std::string& name) const {
            return instance->GetFieldUVE(name).value_or(UVScript::ValueUVE{});
        }
    };
    RunningUVE RunUVE(const BlueprintAssetUVE& blueprint, const EntityUVE entity, Core::ScriptServicesUVE* services = nullptr) {
        RunningUVE run;
        run.host = std::make_unique<Core::UVScriptObjectHostUVE>(entityManager, nullptr, entity);
        run.host->SetServicesUVE(services);
        const std::string source = Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Object3D").source;
        const UVScript::CompileResultUVE compiled = UVScript::CompileUVScriptSourceUVE(source, *run.host);
        for (const UVScript::DiagnosticUVE& d : compiled.diagnostics) {
            ADD_FAILURE() << "line " << d.at.line << ": " << d.message << "\n" << source;
        }
        if (compiled.IsSuccessUVE()) {
            run.instance = std::make_unique<UVScript::ScriptInstanceUVE>(compiled.program, *run.host);
        }
        return run;
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
    EXPECT_NE(translated.source.find("if (input.pressed(\"Jump\")):"), std::string::npos) << translated.source;
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

TEST_F(BlueprintUVETest, AddMovementInputAndJumpSteerTheCharacter) {
    // Event Tick -> Add Movement Input (its forward, scale 1) -> Jump.
    BlueprintAssetUVE blueprint;
    blueprint.nodes = {NodeUVE(1U, "event_tick"), NodeUVE(2U, "add_movement_input"), NodeUVE(3U, "get_forward_vector"),
                       NodeUVE(4U, "character_jump")};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{3U, 0U, 2U, 1U}, BlueprintWireUVE{2U, 0U, 4U, 0U}};
    const EntityUVE entity = MakeEverythingUVE();
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entity};
    const UVScript::CompileResultUVE compiled =
        UVScript::CompileUVScriptSourceUVE(Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Character3D").source, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    UVScript::ScriptInstanceUVE instance(compiled.program, host);
    const UVScript::ValueUVE dt[] = {0.016};
    ASSERT_TRUE(instance.RaiseEventUVE("tick", dt));
    const CharacterControllerComponentUVE& body = entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity);
    // Forward is -Z for an unturned object; the motion the step reads carries it, and the jump.
    EXPECT_NEAR(body.scriptMove.z, -1.0F, 1e-5F);
    EXPECT_TRUE(body.scriptJump);
    const Physics::CharacterMotionInputUVE motion = ResolveCharacterMotionUVE(entityManager, entity);
    EXPECT_NEAR(motion.move.z, -1.0F, 1e-5F);
    EXPECT_TRUE(motion.jumpPressed);
}

TEST_F(BlueprintUVETest, RotationNodesTurnTheObject) {
    BlueprintAssetUVE blueprint;
    BlueprintNodeUVE yaw = NodeUVE(2U, "add_yaw");
    yaw.inputValues = {"", "90"};
    blueprint.nodes = {NodeUVE(1U, "event_ready"), yaw};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}};
    const EntityUVE entity = MakeEverythingUVE();
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entity};
    const UVScript::CompileResultUVE compiled =
        UVScript::CompileUVScriptSourceUVE(Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Object3D").source, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    UVScript::ScriptInstanceUVE instance(compiled.program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready", {}));
    // A quarter turn left about up: forward (-Z) now points along -X.
    const Math::Vector3UVE forward = Math::RotateVectorUVE(entityManager.GetComponentUVE<TransformComponentUVE>(entity).localRotation,
                                                          Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    EXPECT_NEAR(forward.x, -1.0F, 1e-4F);
    EXPECT_NEAR(forward.z, 0.0F, 1e-4F);
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

TEST_F(BlueprintUVETest, GateDoNAndMultiGateCountTheirWay) {
    // Tick -> Gate (Enter) -> Do N (3) -> count += 1 ; Ready closes nothing, the gate starts open.
    BlueprintAssetUVE blueprint;
    blueprint.variables = {{"hits", BlueprintPinTypeUVE::Int, "0", false}, {"lastGate", BlueprintPinTypeUVE::Int, "0", false}};
    BlueprintNodeUVE add = NodeUVE(5U, "int_add");
    add.inputValues = {"", "1"};
    BlueprintNodeUVE close = NodeUVE(8U, "set_variable", "lastGate");
    close.inputValues = {"", "9"};
    blueprint.nodes = {NodeUVE(1U, "event_tick"), NodeUVE(2U, "gate"), NodeUVE(3U, "do_n"), NodeUVE(4U, "set_variable", "hits"),
                       NodeUVE(6U, "get_variable", "hits"), add, NodeUVE(7U, "event_custom"), close};
    blueprint.nodes[2].inputValues = {"", "3"};
    blueprint.nodes[6].inputValues = {"Shut"};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{2U, 0U, 3U, 0U}, BlueprintWireUVE{3U, 0U, 4U, 0U},
                       BlueprintWireUVE{6U, 0U, 5U, 0U}, BlueprintWireUVE{5U, 0U, 4U, 1U},
                       // Custom Event "Shut" -> Gate.Close, then records it.
                       BlueprintWireUVE{7U, 0U, 2U, 2U}};
    const RunningUVE run = RunUVE(blueprint, MakeEverythingUVE());
    ASSERT_NE(run.instance, nullptr);
    const UVScript::ValueUVE dt[] = {0.016};
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(run.instance->RaiseEventUVE("tick", dt));
    }
    EXPECT_EQ(run.FieldUVE("hits"), UVScript::ValueUVE{std::int64_t{3}}) << "Do N lets three through";
    const UVScript::ValueUVE shut[] = {std::string("Shut"), std::string()};
    ASSERT_TRUE(run.instance->RaiseEventUVE("custom_event", shut));
    EXPECT_EQ(run.FieldUVE("gate2"), UVScript::ValueUVE{false}) << "the custom event closed the gate";
}

TEST_F(BlueprintUVETest, MultiGateAndSwitchPickTheirOutputs) {
    BlueprintAssetUVE blueprint;
    blueprint.variables = {{"last", BlueprintPinTypeUVE::Int, "-1", false}, {"word", BlueprintPinTypeUVE::Int, "-1", false}};
    std::vector<BlueprintNodeUVE> nodes = {NodeUVE(1U, "event_tick"), NodeUVE(2U, "multi_gate")};
    std::vector<BlueprintWireUVE> wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}};
    for (std::uint32_t out = 0U; out < 3U; ++out) {
        BlueprintNodeUVE set = NodeUVE(10U + out, "set_variable", "last");
        set.inputValues = {"", std::to_string(out)};
        nodes.push_back(set);
        wires.push_back(BlueprintWireUVE{2U, out, 10U + out, 0U});
    }
    // Ready -> Switch on String ("B" picks Case 1).
    BlueprintNodeUVE sw = NodeUVE(20U, "switch_string");
    sw.inputValues = {"", "B"};
    BlueprintNodeUVE one = NodeUVE(21U, "set_variable", "word");
    one.inputValues = {"", "1"};
    BlueprintNodeUVE other = NodeUVE(22U, "set_variable", "word");
    other.inputValues = {"", "7"};
    nodes.push_back(NodeUVE(19U, "event_ready"));
    nodes.push_back(sw);
    nodes.push_back(one);
    nodes.push_back(other);
    wires.push_back(BlueprintWireUVE{19U, 0U, 20U, 0U});
    wires.push_back(BlueprintWireUVE{20U, 1U, 21U, 0U});
    wires.push_back(BlueprintWireUVE{20U, 4U, 22U, 0U});
    blueprint.nodes = nodes;
    blueprint.wires = wires;
    const RunningUVE run = RunUVE(blueprint, MakeEverythingUVE());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEventUVE("ready", {}));
    EXPECT_EQ(run.FieldUVE("word"), UVScript::ValueUVE{std::int64_t{1}});
    const UVScript::ValueUVE dt[] = {0.016};
    std::vector<std::int64_t> seen;
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(run.instance->RaiseEventUVE("tick", dt));
        seen.push_back(std::get<std::int64_t>(run.FieldUVE("last")));
    }
    // Outputs 0, 1, 2, then the unwired 3 (nothing), then round again to 0.
    EXPECT_EQ(seen, (std::vector<std::int64_t>{0, 1, 2, 2, 0}));
}

TEST_F(BlueprintUVETest, TimersFireRepeatedlyAndClearStopsThem) {
    // Ready -> Set Timer ("Spawn", 0.5 s, looping). On Timer "Spawn" -> count += 1.
    BlueprintAssetUVE blueprint;
    blueprint.variables = {{"count", BlueprintPinTypeUVE::Int, "0", false}};
    BlueprintNodeUVE set = NodeUVE(2U, "set_timer");
    set.inputValues = {"", "Spawn", "0.5", "true"};
    BlueprintNodeUVE add = NodeUVE(6U, "int_add");
    add.inputValues = {"", "1"};
    BlueprintNodeUVE onTimer = NodeUVE(3U, "event_timer");
    onTimer.inputValues = {"Spawn"};
    blueprint.nodes = {NodeUVE(1U, "event_ready"), set, onTimer, NodeUVE(4U, "set_variable", "count"), NodeUVE(5U, "get_variable", "count"), add};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{3U, 0U, 4U, 0U}, BlueprintWireUVE{5U, 0U, 6U, 0U},
                       BlueprintWireUVE{6U, 0U, 4U, 1U}};
    const RunningUVE run = RunUVE(blueprint, MakeEverythingUVE());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEventUVE("ready", {}));
    const auto advance = [&](const double seconds) {
        for (const std::string& id : run.host->AdvanceTimersUVE(seconds)) {
            const UVScript::ValueUVE args[] = {id};
            static_cast<void>(run.instance->RaiseEventUVE("timer", args));
        }
    };
    for (int frame = 0; frame < 10; ++frame) {
        advance(0.26);   // 2.6 s: five periods
    }
    EXPECT_EQ(run.FieldUVE("count"), UVScript::ValueUVE{std::int64_t{5}});
    EXPECT_EQ(run.host->CallFunctionUVE("timers.clear", std::vector<UVScript::ValueUVE>{std::string("Spawn")}), UVScript::ValueUVE{true});
    advance(3.0);
    EXPECT_EQ(run.FieldUVE("count"), UVScript::ValueUVE{std::int64_t{5}}) << "a cleared timer never fires";
}

TEST_F(BlueprintUVETest, ARetriggerableDelayRestartsItsWait) {
    BlueprintAssetUVE blueprint;
    blueprint.variables = {{"done", BlueprintPinTypeUVE::Int, "0", false}};
    BlueprintNodeUVE delay = NodeUVE(2U, "retriggerable_delay");
    delay.inputValues = {"", "1.0"};
    BlueprintNodeUVE set = NodeUVE(3U, "set_variable", "done");
    set.inputValues = {"", "1"};
    blueprint.nodes = {NodeUVE(4U, "event_custom"), delay, set};
    blueprint.wires = {BlueprintWireUVE{4U, 0U, 2U, 0U}, BlueprintWireUVE{2U, 0U, 3U, 0U}};
    const RunningUVE run = RunUVE(blueprint, MakeEverythingUVE());
    ASSERT_NE(run.instance, nullptr);
    const UVScript::ValueUVE poke[] = {std::string("MyEvent"), std::string()};
    const auto advance = [&](const double seconds) {
        for (const std::string& id : run.host->AdvanceTimersUVE(seconds)) {
            const UVScript::ValueUVE args[] = {id};
            static_cast<void>(run.instance->RaiseEventUVE("timer", args));
        }
    };
    ASSERT_TRUE(run.instance->RaiseEventUVE("custom_event", poke));
    advance(0.8);
    ASSERT_TRUE(run.instance->RaiseEventUVE("custom_event", poke));   // starts the second over
    advance(0.8);
    EXPECT_EQ(run.FieldUVE("done"), UVScript::ValueUVE{std::int64_t{0}}) << "1.6 s, but never 1 s since the last poke";
    advance(0.3);
    EXPECT_EQ(run.FieldUVE("done"), UVScript::ValueUVE{std::int64_t{1}});
}

TEST_F(BlueprintUVETest, ServicesSpawnDestroyTraceEmitAndKeepGameData) {
    Core::ScriptServicesUVE services;
    std::vector<EntityUVE> destroyed;
    std::vector<std::pair<EntityUVE, std::string>> sent;
    const EntityUVE spawned = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(spawned, TransformComponentUVE{});
    std::string spawnedPath;
    services.spawn = [&](const std::string& path, const UVScript::Vec3ValueUVE& at, const UVScript::Vec3ValueUVE&) {
        spawnedPath = path;
        entityManager.GetComponentUVE<TransformComponentUVE>(spawned).localPosition =
            Math::Vector3UVE{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
        return spawned;
    };
    services.destroy = [&](const EntityUVE entity) { destroyed.push_back(entity); };
    services.emit = [&](const EntityUVE target, const std::string& name, const std::string&) { sent.emplace_back(target, name); };
    services.trace = [&](const UVScript::Vec3ValueUVE&, const UVScript::Vec3ValueUVE& to, double, EntityUVE) {
        Core::ScriptServicesUVE::TraceHitUVE hit;
        hit.hit = true;
        hit.point = to;
        hit.normal = UVScript::Vec3ValueUVE{0.0, 1.0, 0.0};
        hit.distance = 4.0;
        hit.entity = spawned;
        return hit;
    };

    // Ready: Spawn Entity at (1,2,3) -> Line Trace down -> Set Object Location (hit object, its hit + 1 up)
    //        -> Call Custom Event "Spawned" -> Destroy Object (the spawned one) -> Set Game Data "Score" = 7.5
    BlueprintAssetUVE blueprint;
    blueprint.variables = {{"hitDistance", BlueprintPinTypeUVE::Float, "0", false}};
    BlueprintNodeUVE spawn = NodeUVE(2U, "spawn_entity");
    spawn.inputValues = {"", "Entities/Crate.uventity", "1,2,3", "0,0,0"};
    BlueprintNodeUVE trace = NodeUVE(3U, "line_trace");
    trace.inputValues = {"", "0,10,0", "0,-10,0"};
    BlueprintNodeUVE call = NodeUVE(5U, "call_custom_event");
    call.inputValues = {"", "Spawned", ""};
    BlueprintNodeUVE score = NodeUVE(7U, "save_set_float");
    score.inputValues = {"", "Score", "7.5"};
    blueprint.nodes = {NodeUVE(1U, "event_ready"), spawn, trace, NodeUVE(4U, "set_variable", "hitDistance"), call,
                       NodeUVE(6U, "destroy_object"), score, NodeUVE(8U, "object_set_location")};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{2U, 0U, 3U, 0U}, BlueprintWireUVE{3U, 0U, 4U, 0U},
                       BlueprintWireUVE{3U, 5U, 4U, 1U},   // Distance
                       BlueprintWireUVE{4U, 0U, 8U, 0U}, BlueprintWireUVE{3U, 4U, 8U, 1U}, BlueprintWireUVE{3U, 2U, 8U, 2U},
                       BlueprintWireUVE{8U, 0U, 5U, 0U}, BlueprintWireUVE{5U, 0U, 6U, 0U}, BlueprintWireUVE{2U, 1U, 6U, 1U},
                       BlueprintWireUVE{6U, 0U, 7U, 0U}};
    const EntityUVE self = MakeEverythingUVE();
    const RunningUVE run = RunUVE(blueprint, self, &services);
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEventUVE("ready", {})) << run.instance->GetLastErrorUVE();
    EXPECT_EQ(spawnedPath, "Entities/Crate.uventity");
    EXPECT_EQ(run.FieldUVE("hitDistance"), UVScript::ValueUVE{4.0});
    // Moved by Set Object Location to the trace's end.
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(spawned).localPosition.y, -10.0F);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].first, self);
    EXPECT_EQ(sent[0].second, "Spawned");
    ASSERT_EQ(destroyed.size(), 1U);
    EXPECT_EQ(destroyed[0], spawned);
    EXPECT_EQ(services.gameData["Score"], "7.5");
    EXPECT_EQ(run.host->CallFunctionUVE("save.get_float", std::vector<UVScript::ValueUVE>{std::string("Score"), 0.0}), UVScript::ValueUVE{7.5});
    EXPECT_EQ(run.host->CallFunctionUVE("save.get_int", std::vector<UVScript::ValueUVE>{std::string("Missing"), std::int64_t{4}}),
              UVScript::ValueUVE{std::int64_t{4}});
}

TEST_F(BlueprintUVETest, ALifeSpanDestroysTheObjectThroughTheServices) {
    Core::ScriptServicesUVE services;
    std::vector<EntityUVE> destroyed;
    services.destroy = [&](const EntityUVE entity) { destroyed.push_back(entity); };
    const EntityUVE self = MakeEverythingUVE();
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, self};
    host.SetServicesUVE(&services);
    EXPECT_EQ(host.CallFunctionUVE("world.set_life_span", std::vector<UVScript::ValueUVE>{2.0}), UVScript::ValueUVE{true});
    EXPECT_TRUE(host.AdvanceTimersUVE(1.5).empty());
    EXPECT_TRUE(destroyed.empty());
    EXPECT_TRUE(host.AdvanceTimersUVE(1.0).empty()) << "a life span raises no timer event";
    ASSERT_EQ(destroyed.size(), 1U);
    EXPECT_EQ(destroyed[0], self);
    // Scripts cannot touch the life span's own timer.
    EXPECT_EQ(host.CallFunctionUVE("timers.set", std::vector<UVScript::ValueUVE>{std::string("__life_span"), 1.0, false}),
              UVScript::ValueUVE{false});
}

TEST_F(BlueprintUVETest, ObjectsFindTheirTagsChildrenAndEachOther) {
    const EntityUVE self = MakeEverythingUVE();
    const EntityUVE enemy = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(enemy, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(enemy, NameComponentUVE{"Enemy_1"});
    GameplayTagComponentUVE tags;
    tags.tags = {"Enemy"};
    entityManager.AddComponentUVE<GameplayTagComponentUVE>(enemy, tags);
    HierarchyComponentUVE link;
    link.parent = enemy;
    const EntityUVE child = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<HierarchyComponentUVE>(child, link);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, self};
    const auto call = [&](const std::string_view name, std::vector<UVScript::ValueUVE> args) { return host.CallFunctionUVE(name, args); };
    const UVScript::ValueUVE found = call("world.find_tag", {std::string("Enemy")});
    ASSERT_TRUE(std::holds_alternative<UVScript::ObjectRefUVE>(found));
    EXPECT_NE(std::get<UVScript::ObjectRefUVE>(found).id, 0U);
    EXPECT_EQ(call("object.name", {found}), UVScript::ValueUVE{std::string("Enemy_1")});
    EXPECT_EQ(call("object.set_position", {found, UVScript::Vec3ValueUVE{3.0, 0.0, 4.0}}), UVScript::ValueUVE{true});
    EXPECT_EQ(call("object.distance", {call("world.self", {}), found}), UVScript::ValueUVE{5.0});
    const UVScript::ValueUVE children = call("object.children", {found});
    const auto& list = std::get<std::shared_ptr<const UVScript::CollectionValueUVE>>(children);
    ASSERT_EQ(list->items.size(), 1U);
    EXPECT_EQ(call("object.parent", {list->items[0]}), found);
    EXPECT_EQ(call("world.find_tag", {std::string("Nobody")}), UVScript::ValueUVE{UVScript::ObjectRefUVE{}});
    EXPECT_EQ(call("world.is_valid", {UVScript::ValueUVE{UVScript::ObjectRefUVE{}}}), UVScript::ValueUVE{false});
    // Without services the world calls fail closed.
    EXPECT_EQ(call("world.spawn", {std::string("x.uventity"), UVScript::Vec3ValueUVE{}, UVScript::Vec3ValueUVE{}}),
              UVScript::ValueUVE{UVScript::ObjectRefUVE{}});
    EXPECT_EQ(call("game.quit", {}), UVScript::ValueUVE{false});
    EXPECT_EQ(call("math.select_string", {false, std::string("A"), std::string("B")}), UVScript::ValueUVE{std::string("B")});
}

} // namespace
} // namespace UVE::Scene::Tests
