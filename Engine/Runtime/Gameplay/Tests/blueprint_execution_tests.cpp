// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "camera_component.h"
#include "light_component.h"
#include "hierarchy_component.h"
#include "name_component.h"
#include "ui_button_component.h"
#include "ui_text_component.h"
#include "ui_tween_component.h"

#include "animation_graph_component.h"
#include "animation_sequencer_component.h"
#include "nav_seeker_3d.h"
#include "particle_emitter_component.h"
#include "surface_instance_component.h"
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
#include "montage_sequencer_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_object_host.h"

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
        entityManager.AddComponentUVE<MontageSequencerComponentUVE>(entity, MontageSequencerComponentUVE{});
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
        entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(entity, ParticleEmitterComponentUVE{});
        entityManager.AddComponentUVE<SurfaceInstanceComponentUVE>(entity, SurfaceInstanceComponentUVE{});
        entityManager.AddComponentUVE<AnimationSequencerComponentUVE>(entity, AnimationSequencerComponentUVE{});
        entityManager.AddComponentUVE<NavSeeker3DComponentUVE>(entity, NavSeeker3DComponentUVE{});
        return entity;
    }

    /// Translates and compiles; every diagnostic fails the test with the source around it.
    void ExpectCompilesUVE(const BlueprintAssetUVE& blueprint, const std::string& what) {
        const EntityUVE entity = MakeEverythingUVE();
        Core::BlueprintObjectHostUVE host{entityManager, nullptr, entity};
        const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP_Test", "Character3D");
        for (const auto& [node, problem] : translated.problems) {
            ADD_FAILURE() << what << ": node " << node << ": " << problem;
        }
        const Blueprint::CompileResultUVE result = Blueprint::CompileUVScriptSourceUVE(translated.source, host);
        for (const Blueprint::DiagnosticUVE& d : result.diagnostics) {
            ADD_FAILURE() << what << " line " << d.at.line << ": " << d.message << "\n" << translated.source;
        }
    }

    /// A graph made to run: its host (with `services`), its program and a running instance.
    struct RunningUVE final {
        std::unique_ptr<Core::BlueprintObjectHostUVE> host;
        std::unique_ptr<Blueprint::ScriptInstanceUVE> instance;
        [[nodiscard]] Blueprint::ValueUVE FieldUVE(const std::string& name) const {
            return instance->GetFieldUVE(name).value_or(Blueprint::ValueUVE{});
        }
    };
    RunningUVE RunUVE(const BlueprintAssetUVE& blueprint, const EntityUVE entity, Core::ScriptServicesUVE* services = nullptr) {
        RunningUVE run;
        run.host = std::make_unique<Core::BlueprintObjectHostUVE>(entityManager, nullptr, entity);
        run.host->SetServicesUVE(services);
        const std::string source = Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Object3D").source;
        const Blueprint::CompileResultUVE compiled = Blueprint::CompileUVScriptSourceUVE(source, *run.host);
        for (const Blueprint::DiagnosticUVE& d : compiled.diagnostics) {
            ADD_FAILURE() << "line " << d.at.line << ": " << d.message << "\n" << source;
        }
        if (compiled.IsSuccessUVE()) {
            run.instance = std::make_unique<Blueprint::ScriptInstanceUVE>(compiled.program, *run.host);
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
        // An array input is wired from a variable of its type (Add, Remove and Clear change it in place).
        const std::uint32_t arrayPin = type.role == BlueprintNodeRoleUVE::Pure ? 0U : 1U;
        const bool takesArray = arrayPin < type.inputs.size() && Asset::IsBlueprintArrayTypeUVE(type.inputs[arrayPin].type);
        if (takesArray) {
            blueprint.variables.push_back({"items", type.inputs[arrayPin].type, "", false});
            blueprint.nodes.push_back(NodeUVE(9U, "get_variable", "items"));
            blueprint.wires.push_back(BlueprintWireUVE{9U, 0U, 2U, arrayPin});
        }
        const bool needsObject = type.key == "hide_object" || type.key == "show_object" || type.key == "is_valid" ||
                                 type.key == "ui_add_to_viewport";
        if (type.role == BlueprintNodeRoleUVE::Pure) {
            // Its first output stored in a variable of that type.
            blueprint.variables.push_back({"result", type.outputs.front().type, "", false});
            blueprint.nodes.push_back(NodeUVE(2U, type.key));
            blueprint.nodes.push_back(NodeUVE(3U, "set_variable", "result"));
            blueprint.wires.push_back(BlueprintWireUVE{1U, 0U, 3U, 0U});
            blueprint.wires.push_back(BlueprintWireUVE{2U, 0U, 3U, 1U});
        } else {
            blueprint.nodes.push_back(NodeUVE(2U, type.key));
            blueprint.wires.push_back(BlueprintWireUVE{1U, 0U, 2U, 0U});
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

TEST_F(BlueprintUVETest, EveryNodeHasItsOwnTitleAndKey) {
    // The editor shows nodes by title and old files may be read back by it: two nodes with one
    // title would save as whichever came last.
    std::set<std::string> titles;
    std::set<std::string> keys;
    for (const Asset::BlueprintNodeTypeUVE& type : Asset::GetBlueprintNodeTypesUVE()) {
        if (type.role == BlueprintNodeRoleUVE::GetVariable || type.role == BlueprintNodeRoleUVE::SetVariable) {
            continue;
        }
        EXPECT_TRUE(titles.insert(type.title).second) << "two nodes are titled " << type.title;
        EXPECT_TRUE(keys.insert(type.key).second) << "two nodes have the key " << type.key;
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
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entity};
    const Blueprint::CompileResultUVE compiled =
        Blueprint::CompileUVScriptSourceUVE(Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Object3D").source, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    Blueprint::ScriptInstanceUVE instance(compiled.program, host);
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
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entity};
    const Blueprint::CompileResultUVE compiled =
        Blueprint::CompileUVScriptSourceUVE(Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Character3D").source, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    Blueprint::ScriptInstanceUVE instance(compiled.program, host);
    const Blueprint::ValueUVE dt[] = {0.016};
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
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entity};
    const Blueprint::CompileResultUVE compiled =
        Blueprint::CompileUVScriptSourceUVE(Asset::TranslateBlueprintToUVScriptUVE(blueprint, "BP", "Object3D").source, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());
    Blueprint::ScriptInstanceUVE instance(compiled.program, host);
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
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entity};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::ValueUVE> args) {
        return host.CallFunctionUVE(name, args);
    };
    const auto& graph = entityManager.GetComponentUVE<AnimationGraphComponentUVE>(entity);
    EXPECT_EQ(call("anim.set_bool", {std::string("IsJumping"), true}), Blueprint::ValueUVE{true});
    EXPECT_EQ(graph.parameters[1].value, 1.0F);
    EXPECT_EQ(call("anim.get_bool", {std::string("IsJumping")}), Blueprint::ValueUVE{true});
    EXPECT_EQ(call("anim.trigger", {std::string("Jump")}), Blueprint::ValueUVE{true});
    EXPECT_EQ(graph.parameters[0].value, 1.0F);
    // The character writes Speed every frame: a script may read it, not set it.
    EXPECT_EQ(call("anim.set_float", {std::string("Speed"), 3.0}), Blueprint::ValueUVE{false});
    EXPECT_EQ(call("anim.set_float", {std::string("Missing"), 3.0}), Blueprint::ValueUVE{false});
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
    const Blueprint::ValueUVE dt[] = {0.016};
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(run.instance->RaiseEventUVE("tick", dt));
    }
    EXPECT_EQ(run.FieldUVE("hits"), Blueprint::ValueUVE{std::int64_t{3}}) << "Do N lets three through";
    const Blueprint::ValueUVE shut[] = {std::string("Shut"), std::string()};
    ASSERT_TRUE(run.instance->RaiseEventUVE("custom_event", shut));
    EXPECT_EQ(run.FieldUVE("gate2"), Blueprint::ValueUVE{false}) << "the custom event closed the gate";
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
    EXPECT_EQ(run.FieldUVE("word"), Blueprint::ValueUVE{std::int64_t{1}});
    const Blueprint::ValueUVE dt[] = {0.016};
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
            const Blueprint::ValueUVE args[] = {id};
            static_cast<void>(run.instance->RaiseEventUVE("timer", args));
        }
    };
    for (int frame = 0; frame < 10; ++frame) {
        advance(0.26);   // 2.6 s: five periods
    }
    EXPECT_EQ(run.FieldUVE("count"), Blueprint::ValueUVE{std::int64_t{5}});
    EXPECT_EQ(run.host->CallFunctionUVE("timers.clear", std::vector<Blueprint::ValueUVE>{std::string("Spawn")}), Blueprint::ValueUVE{true});
    advance(3.0);
    EXPECT_EQ(run.FieldUVE("count"), Blueprint::ValueUVE{std::int64_t{5}}) << "a cleared timer never fires";
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
    const Blueprint::ValueUVE poke[] = {std::string("MyEvent"), std::string()};
    const auto advance = [&](const double seconds) {
        for (const std::string& id : run.host->AdvanceTimersUVE(seconds)) {
            const Blueprint::ValueUVE args[] = {id};
            static_cast<void>(run.instance->RaiseEventUVE("timer", args));
        }
    };
    ASSERT_TRUE(run.instance->RaiseEventUVE("custom_event", poke));
    advance(0.8);
    ASSERT_TRUE(run.instance->RaiseEventUVE("custom_event", poke));   // starts the second over
    advance(0.8);
    EXPECT_EQ(run.FieldUVE("done"), Blueprint::ValueUVE{std::int64_t{0}}) << "1.6 s, but never 1 s since the last poke";
    advance(0.3);
    EXPECT_EQ(run.FieldUVE("done"), Blueprint::ValueUVE{std::int64_t{1}});
}

TEST_F(BlueprintUVETest, ServicesSpawnDestroyTraceEmitAndKeepGameData) {
    Core::ScriptServicesUVE services;
    std::vector<EntityUVE> destroyed;
    std::vector<std::pair<EntityUVE, std::string>> sent;
    const EntityUVE spawned = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(spawned, TransformComponentUVE{});
    std::string spawnedPath;
    services.spawn = [&](const std::string& path, const Blueprint::Vec3ValueUVE& at, const Blueprint::Vec3ValueUVE&) {
        spawnedPath = path;
        entityManager.GetComponentUVE<TransformComponentUVE>(spawned).localPosition =
            Math::Vector3UVE{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
        return spawned;
    };
    services.destroy = [&](const EntityUVE entity) { destroyed.push_back(entity); };
    services.emit = [&](const EntityUVE target, const std::string& name, const std::string&) { sent.emplace_back(target, name); };
    services.trace = [&](const Blueprint::Vec3ValueUVE&, const Blueprint::Vec3ValueUVE& to, double, EntityUVE) {
        Core::ScriptServicesUVE::TraceHitUVE hit;
        hit.hit = true;
        hit.point = to;
        hit.normal = Blueprint::Vec3ValueUVE{0.0, 1.0, 0.0};
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
    EXPECT_EQ(run.FieldUVE("hitDistance"), Blueprint::ValueUVE{4.0});
    // Moved by Set Object Location to the trace's end.
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<TransformComponentUVE>(spawned).localPosition.y, -10.0F);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].first, self);
    EXPECT_EQ(sent[0].second, "Spawned");
    ASSERT_EQ(destroyed.size(), 1U);
    EXPECT_EQ(destroyed[0], spawned);
    EXPECT_EQ(services.gameData["Score"], "7.5");
    EXPECT_EQ(run.host->CallFunctionUVE("save.get_float", std::vector<Blueprint::ValueUVE>{std::string("Score"), 0.0}), Blueprint::ValueUVE{7.5});
    EXPECT_EQ(run.host->CallFunctionUVE("save.get_int", std::vector<Blueprint::ValueUVE>{std::string("Missing"), std::int64_t{4}}),
              Blueprint::ValueUVE{std::int64_t{4}});
}

TEST_F(BlueprintUVETest, ALifeSpanDestroysTheObjectThroughTheServices) {
    Core::ScriptServicesUVE services;
    std::vector<EntityUVE> destroyed;
    services.destroy = [&](const EntityUVE entity) { destroyed.push_back(entity); };
    const EntityUVE self = MakeEverythingUVE();
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, self};
    host.SetServicesUVE(&services);
    EXPECT_EQ(host.CallFunctionUVE("world.set_life_span", std::vector<Blueprint::ValueUVE>{2.0}), Blueprint::ValueUVE{true});
    EXPECT_TRUE(host.AdvanceTimersUVE(1.5).empty());
    EXPECT_TRUE(destroyed.empty());
    EXPECT_TRUE(host.AdvanceTimersUVE(1.0).empty()) << "a life span raises no timer event";
    ASSERT_EQ(destroyed.size(), 1U);
    EXPECT_EQ(destroyed[0], self);
    // Scripts cannot touch the life span's own timer.
    EXPECT_EQ(host.CallFunctionUVE("timers.set", std::vector<Blueprint::ValueUVE>{std::string("__life_span"), 1.0, false}),
              Blueprint::ValueUVE{false});
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
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, self};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::ValueUVE> args) { return host.CallFunctionUVE(name, args); };
    const Blueprint::ValueUVE found = call("world.find_tag", {std::string("Enemy")});
    ASSERT_TRUE(std::holds_alternative<Blueprint::ObjectRefUVE>(found));
    EXPECT_NE(std::get<Blueprint::ObjectRefUVE>(found).id, 0U);
    EXPECT_EQ(call("object.name", {found}), Blueprint::ValueUVE{std::string("Enemy_1")});
    EXPECT_EQ(call("object.set_position", {found, Blueprint::Vec3ValueUVE{3.0, 0.0, 4.0}}), Blueprint::ValueUVE{true});
    EXPECT_EQ(call("object.distance", {call("world.self", {}), found}), Blueprint::ValueUVE{5.0});
    const Blueprint::ValueUVE children = call("object.children", {found});
    const auto& list = std::get<std::shared_ptr<const Blueprint::CollectionValueUVE>>(children);
    ASSERT_EQ(list->items.size(), 1U);
    EXPECT_EQ(call("object.parent", {list->items[0]}), found);
    EXPECT_EQ(call("world.find_tag", {std::string("Nobody")}), Blueprint::ValueUVE{Blueprint::ObjectRefUVE{}});
    EXPECT_EQ(call("world.is_valid", {Blueprint::ValueUVE{Blueprint::ObjectRefUVE{}}}), Blueprint::ValueUVE{false});
    // Without services the world calls fail closed.
    EXPECT_EQ(call("world.spawn", {std::string("x.uventity"), Blueprint::Vec3ValueUVE{}, Blueprint::Vec3ValueUVE{}}),
              Blueprint::ValueUVE{Blueprint::ObjectRefUVE{}});
    EXPECT_EQ(call("game.quit", {}), Blueprint::ValueUVE{false});
    EXPECT_EQ(call("math.select_string", {false, std::string("A"), std::string("B")}), Blueprint::ValueUVE{std::string("B")});
}

TEST_F(BlueprintUVETest, EffectsLookClipsAndWalkingReachTheirComponents) {
    Core::ScriptServicesUVE services;
    std::int64_t burst = 0;
    std::string sound;
    double shake = 0.0;
    services.burstParticles = [&](const EntityUVE, const std::int64_t count) {
        burst += count;
        return true;
    };
    services.playSound = [&](const std::string& path, const Blueprint::Vec3ValueUVE&, double, const bool spatial) {
        sound = path + (spatial ? "@3d" : "@2d");
        return true;
    };
    services.cameraShake = [&](const double strength, double) { shake = strength; };
    const EntityUVE self = MakeEverythingUVE();
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, self};
    host.SetServicesUVE(&services);
    const auto call = [&](const std::string_view name, std::vector<Blueprint::ValueUVE> args) { return host.CallFunctionUVE(name, args); };
    EXPECT_EQ(call("particles.set_active", {false}), Blueprint::ValueUVE{true});
    EXPECT_FALSE(entityManager.GetComponentUVE<ParticleEmitterComponentUVE>(self).emitting);
    EXPECT_EQ(call("particles.burst", {std::int64_t{25}}), Blueprint::ValueUVE{true});
    EXPECT_EQ(burst, 25);
    EXPECT_EQ(call("audio.play_at", {std::string("Sounds/Hit.wav"), Blueprint::Vec3ValueUVE{1.0, 0.0, 0.0}, 1.0}), Blueprint::ValueUVE{true});
    EXPECT_EQ(sound, "Sounds/Hit.wav@3d");
    EXPECT_EQ(call("camera.shake", {0.5, 1.0}), Blueprint::ValueUVE{true});
    EXPECT_DOUBLE_EQ(shake, 0.5);
    EXPECT_EQ(call("render.set_material", {std::string("Materials/Red.uvmat")}), Blueprint::ValueUVE{true});
    EXPECT_EQ(entityManager.GetComponentUVE<SurfaceInstanceComponentUVE>(self).materialOverridePath, "Materials/Red.uvmat");
    EXPECT_EQ(call("render.set_transparency", {2.0}), Blueprint::ValueUVE{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<SurfaceInstanceComponentUVE>(self).transparency, 1.0F) << "clamped";
    // Without the game's files a clip is played by the one it has.
    host.SetServicesUVE(nullptr);
    EXPECT_EQ(call("sequencer.play", {std::string()}), Blueprint::ValueUVE{true});
    EXPECT_TRUE(entityManager.GetComponentUVE<AnimationSequencerComponentUVE>(self).playRequested);
    EXPECT_EQ(call("sequencer.is_playing", {}), Blueprint::ValueUVE{true});
    EXPECT_EQ(call("sequencer.set_rate", {-1.0}), Blueprint::ValueUVE{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<AnimationSequencerComponentUVE>(self).speed, -1.0F);
    EXPECT_EQ(call("sequencer.stop", {}), Blueprint::ValueUVE{true});
    EXPECT_EQ(call("sequencer.is_playing", {}), Blueprint::ValueUVE{false});
    EXPECT_EQ(call("ai.move_to", {Blueprint::Vec3ValueUVE{4.0, 0.0, 2.0}}), Blueprint::ValueUVE{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(self).targetPosition.x, 4.0F);
    EXPECT_EQ(call("ai.status", {}), Blueprint::ValueUVE{std::string("Idle")});
    EXPECT_EQ(call("camera.shake", {0.5, 1.0}), Blueprint::ValueUVE{false}) << "no services: fails closed";
}

TEST_F(BlueprintUVETest, ForEachWalksASplitTextIntoAnArray) {
    // Ready -> For Each (String) over Split("red,green,blue", ",") -> Add the item to `seen`, and the
    // index into `last`; Completed -> Set `label` = Format Text("{0} of {1}", Length(seen), "3").
    BlueprintAssetUVE blueprint;
    blueprint.variables = {{"seen", BlueprintPinTypeUVE::StringArray, "", false},
                           {"last", BlueprintPinTypeUVE::Int, "-1", false},
                           {"label", BlueprintPinTypeUVE::String, "", false}};
    BlueprintNodeUVE split = NodeUVE(3U, "string_split");
    split.inputValues = {"red,green,blue", ","};
    BlueprintNodeUVE format = NodeUVE(9U, "format_text");
    format.inputValues = {"{0} of {1}", "", "3"};
    blueprint.nodes = {NodeUVE(1U, "event_ready"), NodeUVE(2U, "for_each_String"), split, NodeUVE(4U, "array_add_String"),
                       NodeUVE(5U, "get_variable", "seen"), NodeUVE(6U, "set_variable", "last"), NodeUVE(7U, "set_variable", "label"),
                       NodeUVE(8U, "array_length_String"), format, NodeUVE(10U, "int_to_string"), NodeUVE(11U, "get_variable", "seen")};
    blueprint.wires = {BlueprintWireUVE{1U, 0U, 2U, 0U}, BlueprintWireUVE{3U, 0U, 2U, 1U},
                       // body: Add(seen, Element) -> last = Index
                       BlueprintWireUVE{2U, 0U, 4U, 0U}, BlueprintWireUVE{5U, 0U, 4U, 1U}, BlueprintWireUVE{2U, 1U, 4U, 2U},
                       BlueprintWireUVE{4U, 0U, 6U, 0U}, BlueprintWireUVE{2U, 2U, 6U, 1U},
                       // completed: label = Format(str(Length(seen)))
                       BlueprintWireUVE{2U, 3U, 7U, 0U}, BlueprintWireUVE{11U, 0U, 8U, 0U}, BlueprintWireUVE{8U, 0U, 10U, 0U},
                       BlueprintWireUVE{10U, 0U, 9U, 1U}, BlueprintWireUVE{9U, 0U, 7U, 1U}};
    const RunningUVE run = RunUVE(blueprint, MakeEverythingUVE());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEventUVE("ready", {})) << run.instance->GetLastErrorUVE();
    EXPECT_EQ(run.FieldUVE("last"), Blueprint::ValueUVE{std::int64_t{2}});
    EXPECT_EQ(run.FieldUVE("label"), Blueprint::ValueUVE{std::string("3 of 3")});
    // Kept by value: FieldUVE returns a copy, and a reference into it would dangle.
    const Blueprint::ValueUVE seenValue = run.FieldUVE("seen");
    const auto seen = std::get<std::shared_ptr<const Blueprint::CollectionValueUVE>>(seenValue);
    ASSERT_EQ(seen->items.size(), 3U);
    EXPECT_EQ(seen->items[1], Blueprint::ValueUVE{std::string("green")});

    Core::BlueprintObjectHostUVE host{entityManager, nullptr, MakeEverythingUVE()};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::ValueUVE> args) { return host.CallFunctionUVE(name, args); };
    EXPECT_EQ(call("math.wrap", {370.0, 0.0, 360.0}), Blueprint::ValueUVE{10.0});
    EXPECT_EQ(call("math.wrap", {-30.0, 0.0, 360.0}), Blueprint::ValueUVE{330.0});
    EXPECT_EQ(call("math.snap", {2.6, 0.5}), Blueprint::ValueUVE{2.5});
    EXPECT_EQ(call("math.round", {2.5}), Blueprint::ValueUVE{std::int64_t{3}});
    EXPECT_EQ(call("text.to_int", {std::string("x")}), Blueprint::ValueUVE{std::int64_t{0}}) << "not a number reads 0";
    EXPECT_EQ(call("text.replace", {std::string("a-b-c"), std::string("-"), std::string("--")}), Blueprint::ValueUVE{std::string("a--b--c")});
    EXPECT_EQ(call("text.substring", {std::string("hello"), std::int64_t{1}, std::int64_t{99}}), Blueprint::ValueUVE{std::string("ello")});
    const Blueprint::ValueUVE turned = call("math.rotate_about", {Blueprint::Vec3ValueUVE{1.0, 0.0, 0.0}, Blueprint::Vec3ValueUVE{0.0, 1.0, 0.0}, 90.0});
    EXPECT_NEAR(std::get<Blueprint::Vec3ValueUVE>(turned).z, -1.0, 1e-5);
}

TEST_F(BlueprintUVETest, WidgetNodesMoveSizeFadeAndAnimateWidgets) {
    const EntityUVE button = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<NameComponentUVE>(button, NameComponentUVE{"Play"});
    entityManager.AddComponentUVE<UIButtonComponentUVE>(button, UIButtonComponentUVE{});
    UITweenComponentUVE tween;
    tween.playing = false;
    tween.elapsed = 0.7F;
    entityManager.AddComponentUVE<UITweenComponentUVE>(button, tween);
    const EntityUVE label = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<NameComponentUVE>(label, NameComponentUVE{"Title"});
    entityManager.AddComponentUVE<UITextComponentUVE>(label, UITextComponentUVE{});
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, MakeEverythingUVE()};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::ValueUVE> args) { return host.CallFunctionUVE(name, args); };
    EXPECT_EQ(call("ui.set_position", {std::string("Play"), Blueprint::Vec3ValueUVE{40.0, 60.0, 0.0}}), Blueprint::ValueUVE{true});
    EXPECT_EQ(call("ui.set_size", {std::string("Play"), Blueprint::Vec3ValueUVE{200.0, 50.0, 0.0}}), Blueprint::ValueUVE{true});
    const UIButtonComponentUVE& b = entityManager.GetComponentUVE<UIButtonComponentUVE>(button);
    EXPECT_FLOAT_EQ(b.rect.position.y, 60.0F);
    EXPECT_FLOAT_EQ(b.rect.size.x, 200.0F);
    EXPECT_EQ(call("ui.get_size", {std::string("Play")}), (Blueprint::ValueUVE{Blueprint::Vec3ValueUVE{200.0, 50.0, 0.0}}));
    EXPECT_EQ(call("ui.set_size", {std::string("Play"), Blueprint::Vec3ValueUVE{-1.0, 5.0, 0.0}}), Blueprint::ValueUVE{false}) << "no negative size";
    EXPECT_EQ(call("ui.set_position", {std::string("Title"), Blueprint::Vec3ValueUVE{5.0, 6.0, 0.0}}), Blueprint::ValueUVE{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<UITextComponentUVE>(label).positionPixels.x, 5.0F);
    EXPECT_EQ(call("ui.set_opacity", {std::string("Title"), 0.25}), Blueprint::ValueUVE{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<UITextComponentUVE>(label).alpha, 0.25F);
    EXPECT_EQ(call("ui.set_font_size", {std::string("Title"), 40.0}), Blueprint::ValueUVE{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<UITextComponentUVE>(label).fontSize, 40.0F);
    EXPECT_EQ(call("ui.play_tween", {std::string("Play")}), Blueprint::ValueUVE{true});
    EXPECT_TRUE(entityManager.GetComponentUVE<UITweenComponentUVE>(button).playing);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<UITweenComponentUVE>(button).elapsed, 0.0F);
    EXPECT_EQ(call("ui.is_hovered", {std::string("Play")}), Blueprint::ValueUVE{false});
    EXPECT_EQ(call("ui.set_opacity", {std::string("Nobody"), 0.5}), Blueprint::ValueUVE{false});
}

TEST_F(BlueprintUVETest, NetworkingNodesDelegateToEngineRuntimeNetworking) {
    // Verify the Networking nodes defined in Engine/Editor/EditorCore/Blueprint/Nodes/Networking/ are registered in
    // the Blueprint catalog, translate cleanly, and execute against BlueprintObjectHostUVE
    // (which delegates directly to Engine/Runtime/Networking/).
    const Asset::BlueprintNodeTypeUVE* const relayCount = Asset::FindBlueprintNodeTypeUVE("net_relay_peer_count");
    const Asset::BlueprintNodeTypeUVE* const lobbyRoom = Asset::FindBlueprintNodeTypeUVE("net_lobby_room");
    const Asset::BlueprintNodeTypeUVE* const rpcBegin = Asset::FindBlueprintNodeTypeUVE("net_rpc_begin");
    const Asset::BlueprintNodeTypeUVE* const rpcSend = Asset::FindBlueprintNodeTypeUVE("net_rpc_send");
    ASSERT_NE(relayCount, nullptr);
    ASSERT_NE(lobbyRoom, nullptr);
    ASSERT_NE(rpcBegin, nullptr);
    ASSERT_NE(rpcSend, nullptr);
    EXPECT_EQ(relayCount->category, "Networking|Relay");
    EXPECT_EQ(lobbyRoom->category, "Networking|Lobby");
    EXPECT_EQ(rpcBegin->category, "Networking|RPC");

    BlueprintAssetUVE asset;
    asset.variables.push_back(Asset::BlueprintVariableUVE{"peers", BlueprintPinTypeUVE::Int, "99", false});
    asset.variables.push_back(Asset::BlueprintVariableUVE{"room", BlueprintPinTypeUVE::Int, "99", false});
    asset.nodes = {
        NodeUVE(1U, "event_ready"),
        NodeUVE(2U, "net_relay_peer_count"),
        NodeUVE(3U, "set_variable", "peers"),
        NodeUVE(4U, "net_lobby_room"),
        NodeUVE(5U, "set_variable", "room"),
    };
    asset.wires = {
        BlueprintWireUVE{1U, 0U, 3U, 0U},
        BlueprintWireUVE{2U, 0U, 3U, 1U},
        BlueprintWireUVE{3U, 0U, 5U, 0U},
        BlueprintWireUVE{4U, 0U, 5U, 1U},
    };

    const Asset::BlueprintTranslationUVE translated = Asset::TranslateBlueprintToUVScriptUVE(asset, "BP_Net", "Object3D");
    ASSERT_TRUE(translated.problems.empty());
    EXPECT_NE(translated.source.find("relay.peer_count()"), std::string::npos);
    EXPECT_NE(translated.source.find("lobby.room()"), std::string::npos);

    const RunningUVE run = RunUVE(asset, MakeEverythingUVE());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEventUVE("ready"));
    // With no attached relay/lobby peer, Engine/Runtime/Networking/ host bindings fail closed to 0.
    EXPECT_EQ(run.FieldUVE("peers"), Blueprint::ValueUVE{std::int64_t{0}});
    EXPECT_EQ(run.FieldUVE("room"), Blueprint::ValueUVE{std::int64_t{0}});
}

} // namespace
} // namespace UVE::Scene::Tests
