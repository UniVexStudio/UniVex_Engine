// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
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
#include "ragdoll_3d.h"
#include "blueprint_compiler.h"
#include "blueprint_graph_lowering.h"
#include "blueprint_instance.h"
#include "blueprint_object_host.h"

namespace UVE::Scene::Tests {
namespace {

using Asset::BlueprintAsset;
using Asset::BlueprintNodeRole;
using Asset::BlueprintNode;
using Asset::BlueprintPinType;
using Asset::BlueprintWire;

class BlueprintTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    /// An object with every surface a node can reach: a body, a character, a sound, a montage
    /// player, gameplay pools and tags.
    Entity MakeEverything() {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<TransformComponent>(entity, TransformComponent{});
        entityManager.AddComponent<Rigid3DComponent>(entity, Rigid3DComponent{});
        entityManager.AddComponent<CharacterControllerComponent>(entity, CharacterControllerComponent{});
        entityManager.AddComponent<AudioSourceComponent>(entity, AudioSourceComponent{});
        entityManager.AddComponent<MontageSequencerComponent>(entity, MontageSequencerComponent{});
        AnimationGraphComponent graph;
        graph.parameters.push_back(AnimationParameter{"Jump", AnimationParameterType::Trigger});
        graph.parameters.push_back(AnimationParameter{"IsJumping", AnimationParameterType::Bool});
        AnimationParameter speed{"Speed", AnimationParameterType::Float};
        speed.source = AnimationParameterSource::CharacterSpeed;
        graph.parameters.push_back(speed);
        entityManager.AddComponent<AnimationGraphComponent>(entity, graph);
        GameplayAttributesComponent pools;
        EXPECT_TRUE(AddGameplayAttribute(pools, "stamina", 10.0F, 10.0F));
        entityManager.AddComponent<GameplayAttributesComponent>(entity, pools);
        entityManager.AddComponent<GameplayTagComponent>(entity, GameplayTagComponent{});
        entityManager.AddComponent<LightComponent>(entity, LightComponent{});
        entityManager.AddComponent<CameraComponent>(entity, CameraComponent{});
        entityManager.AddComponent<ParticleEmitterComponent>(entity, ParticleEmitterComponent{});
        entityManager.AddComponent<SurfaceInstanceComponent>(entity, SurfaceInstanceComponent{});
        entityManager.AddComponent<AnimationSequencerComponent>(entity, AnimationSequencerComponent{});
        entityManager.AddComponent<NavSeeker3DComponent>(entity, NavSeeker3DComponent{});
        entityManager.AddComponent<Ragdoll3DComponent>(entity, Ragdoll3DComponent{});
        return entity;
    }

    /// Compiles `blueprint` from its graph; every problem and diagnostic fails the test, naming the node.
    void ExpectCompiles(const BlueprintAsset& blueprint, const std::string& what) {
        const Entity entity = MakeEverything();
        Core::BlueprintObjectHost host{entityManager, nullptr, entity};
        const Asset::BlueprintCompilation compiled = Asset::CompileBlueprintGraph(blueprint, "BP_Test", "Character3D", host);
        for (const auto& [node, problem] : compiled.problems) {
            ADD_FAILURE() << what << ": node " << node << ": " << problem;
        }
        for (const Blueprint::Diagnostic& d : compiled.result.diagnostics) {
            ADD_FAILURE() << what << " node " << d.at.line << ": " << d.message;
        }
    }

    /// A graph made to run: its host (with `services`), its program and a running instance.
    struct Running final {
        std::unique_ptr<Core::BlueprintObjectHost> host;
        std::unique_ptr<Blueprint::BlueprintInstance> instance;
        [[nodiscard]] Blueprint::Value Field(const std::string& name) const {
            return instance->GetField(name).value_or(Blueprint::Value{});
        }
    };
    Running Run(const BlueprintAsset& blueprint, const Entity entity, Core::ScriptServices* services = nullptr) {
        Running run;
        run.host = std::make_unique<Core::BlueprintObjectHost>(entityManager, nullptr, entity);
        run.host->SetServices(services);
        const Blueprint::CompileResult compiled = Asset::CompileBlueprintGraph(blueprint, "BP", "Object3D", *run.host).result;
        for (const Blueprint::Diagnostic& d : compiled.diagnostics) {
            ADD_FAILURE() << "node " << d.at.line << ": " << d.message;
        }
        if (compiled.IsSuccess()) {
            run.instance = std::make_unique<Blueprint::BlueprintInstance>(compiled.program, *run.host);
        }
        return run;
    }

    static BlueprintNode Node(const std::uint32_t id, const std::string& type, const std::string& variable = "") {
        BlueprintNode node;
        node.id = id;
        node.type = type;
        node.variable = variable;
        return node;
    }
};

/// A small graph that uses `type` the way the editor would: an event reaching it, or its value stored in a variable.
/// Empty for the variable nodes, which have their own test.
[[nodiscard]] std::optional<BlueprintAsset> PaletteGraph(const Asset::BlueprintNodeType& type) {
    BlueprintAsset blueprint;
    const auto node = [](const std::uint32_t id, const std::string& key, const std::string& variable = "") {
        BlueprintNode made;
        made.id = id;
        made.type = key;
        made.variable = variable;
        return made;
    };
    if (type.role == BlueprintNodeRole::Event) {
        blueprint.nodes = {node(1U, type.key)};
        return blueprint;
    }
    if (type.role == BlueprintNodeRole::GetVariable || type.role == BlueprintNodeRole::SetVariable) {
        return std::nullopt;
    }
    blueprint.nodes.push_back(node(1U, "event_tick"));
    // An array input is wired from a variable of its type (Add, Remove and Clear change it in place).
    const std::uint32_t arrayPin = type.role == BlueprintNodeRole::Pure ? 0U : 1U;
    const bool takesArray = arrayPin < type.inputs.size() && Asset::IsBlueprintArrayType(type.inputs[arrayPin].type);
    if (takesArray) {
        blueprint.variables.push_back({"items", type.inputs[arrayPin].type, "", false});
        blueprint.nodes.push_back(node(9U, "get_variable", "items"));
        blueprint.wires.push_back(BlueprintWire{9U, 0U, 2U, arrayPin});
    }
    const bool needsObject = type.key == "hide_object" || type.key == "show_object" || type.key == "is_valid" ||
                             type.key == "ui_add_to_viewport";
    if (type.role == BlueprintNodeRole::Pure) {
        // Its first output stored in a variable of that type.
        blueprint.variables.push_back({"result", type.outputs.front().type, "", false});
        blueprint.nodes.push_back(node(2U, type.key));
        blueprint.nodes.push_back(node(3U, "set_variable", "result"));
        blueprint.wires.push_back(BlueprintWire{1U, 0U, 3U, 0U});
        blueprint.wires.push_back(BlueprintWire{2U, 0U, 3U, 1U});
    } else {
        blueprint.nodes.push_back(node(2U, type.key));
        blueprint.wires.push_back(BlueprintWire{1U, 0U, 2U, 0U});
        if (needsObject) {
            BlueprintNode find = node(3U, "find_object");
            find.inputValues = {"Door"};
            blueprint.nodes.push_back(find);
            blueprint.wires.push_back(BlueprintWire{3U, 0U, 2U, 1U});
        }
    }
    return blueprint;
}

TEST_F(BlueprintTest, EveryNodeOfThePaletteCompilesFromItsGraph) {
    for (const Asset::BlueprintNodeType& type : Asset::GetBlueprintNodeTypes()) {
        const std::optional<BlueprintAsset> blueprint = PaletteGraph(type);
        if (blueprint.has_value()) {
            ExpectCompiles(*blueprint, type.key);
        }
    }
}

TEST_F(BlueprintTest, EveryNodeHasItsOwnTitleAndKey) {
    // The editor shows nodes by title and old files may be read back by it: two nodes with one
    // title would save as whichever came last.
    std::set<std::string> titles;
    std::set<std::string> keys;
    for (const Asset::BlueprintNodeType& type : Asset::GetBlueprintNodeTypes()) {
        if (type.role == BlueprintNodeRole::GetVariable || type.role == BlueprintNodeRole::SetVariable) {
            continue;
        }
        EXPECT_TRUE(titles.insert(type.title).second) << "two nodes are titled " << type.title;
        EXPECT_TRUE(keys.insert(type.key).second) << "two nodes have the key " << type.key;
    }
}

TEST_F(BlueprintTest, VariablesBecomeFieldsAndExposedOnesExports) {
    BlueprintAsset blueprint;
    blueprint.variables = {{"speed", BlueprintPinType::Float, "6", true},
                           {"jumps", BlueprintPinType::Int, "0", false},
                           {"offset", BlueprintPinType::Vector, "0,1,0", true},
                           {"label", BlueprintPinType::String, "hi", false},
                           {"armed", BlueprintPinType::Bool, "true", false}};
    blueprint.nodes = {Node(1U, "event_ready"), Node(2U, "set_variable", "jumps"), Node(3U, "get_variable", "jumps"),
                       Node(4U, "int_add")};
    blueprint.nodes[3].inputValues = {"", "1"};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{3U, 0U, 4U, 0U}, BlueprintWire{4U, 0U, 2U, 1U}};
    // The variables become fields; the exposed ones exports.
    const Entity self = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, self};
    const Blueprint::CompileResult compiledVariables =
        Asset::CompileBlueprintGraph(blueprint, "BP_Player", "Player3D", host).result;
    ASSERT_TRUE(compiledVariables.IsSuccess());
    std::map<std::string, Blueprint::FieldKind> kinds;
    for (const Blueprint::FieldInfo& field : Blueprint::GetProgramFields(*compiledVariables.program)) {
        kinds[field.name] = field.kind;
    }
    EXPECT_EQ(kinds.at("speed"), Blueprint::FieldKind::Export);
    EXPECT_EQ(kinds.at("offset"), Blueprint::FieldKind::Export);
    EXPECT_EQ(kinds.at("jumps"), Blueprint::FieldKind::Var);
    EXPECT_EQ(kinds.at("label"), Blueprint::FieldKind::Var);
    ExpectCompiles(blueprint, "variables");
}

TEST_F(BlueprintTest, FlowNodesNestTheirChains) {
    BlueprintAsset blueprint;
    blueprint.nodes = {Node(1U, "event_tick"), Node(2U, "branch"), Node(3U, "print"), Node(4U, "for_loop"), Node(5U, "print"),
                       Node(6U, "input_pressed")};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{6U, 0U, 2U, 1U}, BlueprintWire{2U, 0U, 3U, 0U},
                       BlueprintWire{2U, 1U, 4U, 0U}, BlueprintWire{4U, 0U, 5U, 0U}};
    // The print inside the loop is attributed to its node: the program has an instruction for node 5.
    const Entity self = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, self};
    const Asset::BlueprintLowering lowered = Asset::LowerBlueprintGraph(blueprint, "BP", "Object3D");
    ASSERT_EQ(lowered.file.handlers.size(), 1U);
    const Blueprint::Stmt& branch = *lowered.file.handlers[0].body[0];
    ASSERT_EQ(branch.kind, Blueprint::StmtKind::If);
    EXPECT_EQ(branch.at.line, 2U) << "a statement carries its node's id";
    EXPECT_FALSE(branch.elseBody.empty()) << "the False wire makes an else";
    const Blueprint::Stmt& loop = *branch.elseBody[0];
    ASSERT_EQ(loop.kind, Blueprint::StmtKind::For);
    ASSERT_EQ(loop.body.size(), 1U);
    EXPECT_EQ(loop.body[0]->at.line, 5U);
    ExpectCompiles(blueprint, "flow");
}

TEST_F(BlueprintTest, AReadyBlueprintRunsAndMovesItsObject) {
    BlueprintAsset blueprint;
    BlueprintNode set = Node(2U, "set_position");
    set.inputValues = {"", "1,2,3"};
    blueprint.nodes = {Node(1U, "event_ready"), set};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}};
    const Entity entity = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, entity};
    const Blueprint::CompileResult compiled = Asset::CompileBlueprintGraph(blueprint, "BP", "Object3D", host).result;
    ASSERT_TRUE(compiled.IsSuccess());
    Blueprint::BlueprintInstance instance(compiled.program, host);
    static_cast<void>(instance.RaiseEvent("ready", {}));
    const TransformComponent& transform = entityManager.GetComponent<TransformComponent>(entity);
    EXPECT_FLOAT_EQ(transform.localPosition.x, 1.0F);
    EXPECT_FLOAT_EQ(transform.localPosition.z, 3.0F);
}

TEST_F(BlueprintTest, AddMovementInputAndJumpSteerTheCharacter) {
    // Event Tick -> Add Movement Input (its forward, scale 1) -> Jump.
    BlueprintAsset blueprint;
    blueprint.nodes = {Node(1U, "event_tick"), Node(2U, "add_movement_input"), Node(3U, "get_forward_vector"),
                       Node(4U, "character_jump")};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{3U, 0U, 2U, 1U}, BlueprintWire{2U, 0U, 4U, 0U}};
    const Entity entity = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, entity};
    const Blueprint::CompileResult compiled = Asset::CompileBlueprintGraph(blueprint, "BP", "Character3D", host).result;
    ASSERT_TRUE(compiled.IsSuccess());
    Blueprint::BlueprintInstance instance(compiled.program, host);
    const Blueprint::Value dt[] = {0.016};
    ASSERT_TRUE(instance.RaiseEvent("tick", dt));
    const CharacterControllerComponent& body = entityManager.GetComponent<CharacterControllerComponent>(entity);
    // Forward is -Z for an unturned object; the motion the step reads carries it, and the jump.
    EXPECT_NEAR(body.scriptMove.z, -1.0F, 1e-5F);
    EXPECT_TRUE(body.scriptJump);
    const Physics::CharacterMotionInput motion = ResolveCharacterMotion(entityManager, entity);
    EXPECT_NEAR(motion.move.z, -1.0F, 1e-5F);
    EXPECT_TRUE(motion.jumpPressed);
}

TEST_F(BlueprintTest, RotationNodesTurnTheObject) {
    BlueprintAsset blueprint;
    BlueprintNode yaw = Node(2U, "add_yaw");
    yaw.inputValues = {"", "90"};
    blueprint.nodes = {Node(1U, "event_ready"), yaw};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}};
    const Entity entity = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, entity};
    const Blueprint::CompileResult compiled = Asset::CompileBlueprintGraph(blueprint, "BP", "Object3D", host).result;
    ASSERT_TRUE(compiled.IsSuccess());
    Blueprint::BlueprintInstance instance(compiled.program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready", {}));
    // A quarter turn left about up: forward (-Z) now points along -X.
    const Math::Vector3 forward = Math::RotateVector(entityManager.GetComponent<TransformComponent>(entity).localRotation,
                                                          Math::Vector3{0.0F, 0.0F, -1.0F});
    EXPECT_NEAR(forward.x, -1.0F, 1e-4F);
    EXPECT_NEAR(forward.z, 0.0F, 1e-4F);
}

TEST_F(BlueprintTest, AFileRoundTripsAndABadOneIsRefused) {
    BlueprintAsset blueprint;
    blueprint.parentKind = "Player3D";
    blueprint.variables = {{"speed", BlueprintPinType::Float, "6", true}};
    blueprint.nodes = {Node(1U, "event_tick"), Node(2U, "print")};
    blueprint.nodes[1].inputValues = {"", "tick"};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}};
    blueprint.comments.push_back(Asset::BlueprintComment{"Movement", 10.0F, 20.0F, 300.0F, 200.0F, 0x112233FFU});
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "uve_blueprint_round_trip_test.uvbp";
    ASSERT_TRUE(Asset::SaveBlueprintAsset(blueprint, file));
    BlueprintAsset read;
    ASSERT_TRUE(Asset::LoadBlueprintAsset(file, read));
    EXPECT_EQ(read, blueprint);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);

    BlueprintAsset bad = blueprint;
    bad.wires.push_back(BlueprintWire{1U, 0U, 99U, 0U});
    EXPECT_FALSE(Asset::IsBlueprintAssetValid(bad)) << "a wire to no node";
    bad = blueprint;
    bad.variables.push_back(bad.variables.front());
    EXPECT_FALSE(Asset::IsBlueprintAssetValid(bad)) << "two variables with one name";
}

TEST_F(BlueprintTest, BlueprintsSetAnimationGraphParameters) {
    const Entity entity = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, entity};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::Value> args) {
        return host.CallFunction(name, args);
    };
    const auto& graph = entityManager.GetComponent<AnimationGraphComponent>(entity);
    EXPECT_EQ(call("anim.set_bool", {std::string("IsJumping"), true}), Blueprint::Value{true});
    EXPECT_EQ(graph.parameters[1].value, 1.0F);
    EXPECT_EQ(call("anim.get_bool", {std::string("IsJumping")}), Blueprint::Value{true});
    EXPECT_EQ(call("anim.trigger", {std::string("Jump")}), Blueprint::Value{true});
    EXPECT_EQ(graph.parameters[0].value, 1.0F);
    // The character writes Speed every frame: a script may read it, not set it.
    EXPECT_EQ(call("anim.set_float", {std::string("Speed"), 3.0}), Blueprint::Value{false});
    EXPECT_EQ(call("anim.set_float", {std::string("Missing"), 3.0}), Blueprint::Value{false});
}

TEST_F(BlueprintTest, GateDoNAndMultiGateCountTheirWay) {
    // Tick -> Gate (Enter) -> Do N (3) -> count += 1 ; Ready closes nothing, the gate starts open.
    BlueprintAsset blueprint;
    blueprint.variables = {{"hits", BlueprintPinType::Int, "0", false}, {"lastGate", BlueprintPinType::Int, "0", false}};
    BlueprintNode add = Node(5U, "int_add");
    add.inputValues = {"", "1"};
    BlueprintNode close = Node(8U, "set_variable", "lastGate");
    close.inputValues = {"", "9"};
    blueprint.nodes = {Node(1U, "event_tick"), Node(2U, "gate"), Node(3U, "do_n"), Node(4U, "set_variable", "hits"),
                       Node(6U, "get_variable", "hits"), add, Node(7U, "event_custom"), close};
    blueprint.nodes[2].inputValues = {"", "3"};
    blueprint.nodes[6].inputValues = {"Shut"};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{2U, 0U, 3U, 0U}, BlueprintWire{3U, 0U, 4U, 0U},
                       BlueprintWire{6U, 0U, 5U, 0U}, BlueprintWire{5U, 0U, 4U, 1U},
                       // Custom Event "Shut" -> Gate.Close, then records it.
                       BlueprintWire{7U, 0U, 2U, 2U}};
    const Running run = Run(blueprint, MakeEverything());
    ASSERT_NE(run.instance, nullptr);
    const Blueprint::Value dt[] = {0.016};
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(run.instance->RaiseEvent("tick", dt));
    }
    EXPECT_EQ(run.Field("hits"), Blueprint::Value{std::int64_t{3}}) << "Do N lets three through";
    const Blueprint::Value shut[] = {std::string("Shut"), std::string()};
    ASSERT_TRUE(run.instance->RaiseEvent("custom_event", shut));
    EXPECT_EQ(run.Field("gate2"), Blueprint::Value{false}) << "the custom event closed the gate";
}

TEST_F(BlueprintTest, MultiGateAndSwitchPickTheirOutputs) {
    BlueprintAsset blueprint;
    blueprint.variables = {{"last", BlueprintPinType::Int, "-1", false}, {"word", BlueprintPinType::Int, "-1", false}};
    std::vector<BlueprintNode> nodes = {Node(1U, "event_tick"), Node(2U, "multi_gate")};
    std::vector<BlueprintWire> wires = {BlueprintWire{1U, 0U, 2U, 0U}};
    for (std::uint32_t out = 0U; out < 3U; ++out) {
        BlueprintNode set = Node(10U + out, "set_variable", "last");
        set.inputValues = {"", std::to_string(out)};
        nodes.push_back(set);
        wires.push_back(BlueprintWire{2U, out, 10U + out, 0U});
    }
    // Ready -> Switch on String ("B" picks Case 1).
    BlueprintNode sw = Node(20U, "switch_string");
    sw.inputValues = {"", "B"};
    BlueprintNode one = Node(21U, "set_variable", "word");
    one.inputValues = {"", "1"};
    BlueprintNode other = Node(22U, "set_variable", "word");
    other.inputValues = {"", "7"};
    nodes.push_back(Node(19U, "event_ready"));
    nodes.push_back(sw);
    nodes.push_back(one);
    nodes.push_back(other);
    wires.push_back(BlueprintWire{19U, 0U, 20U, 0U});
    wires.push_back(BlueprintWire{20U, 1U, 21U, 0U});
    wires.push_back(BlueprintWire{20U, 4U, 22U, 0U});
    blueprint.nodes = nodes;
    blueprint.wires = wires;
    const Running run = Run(blueprint, MakeEverything());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEvent("ready", {}));
    EXPECT_EQ(run.Field("word"), Blueprint::Value{std::int64_t{1}});
    const Blueprint::Value dt[] = {0.016};
    std::vector<std::int64_t> seen;
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(run.instance->RaiseEvent("tick", dt));
        seen.push_back(std::get<std::int64_t>(run.Field("last")));
    }
    // Outputs 0, 1, 2, then the unwired 3 (nothing), then round again to 0.
    EXPECT_EQ(seen, (std::vector<std::int64_t>{0, 1, 2, 2, 0}));
}

TEST_F(BlueprintTest, TimersFireRepeatedlyAndClearStopsThem) {
    // Ready -> Set Timer ("Spawn", 0.5 s, looping). On Timer "Spawn" -> count += 1.
    BlueprintAsset blueprint;
    blueprint.variables = {{"count", BlueprintPinType::Int, "0", false}};
    BlueprintNode set = Node(2U, "set_timer");
    set.inputValues = {"", "Spawn", "0.5", "true"};
    BlueprintNode add = Node(6U, "int_add");
    add.inputValues = {"", "1"};
    BlueprintNode onTimer = Node(3U, "event_timer");
    onTimer.inputValues = {"Spawn"};
    blueprint.nodes = {Node(1U, "event_ready"), set, onTimer, Node(4U, "set_variable", "count"), Node(5U, "get_variable", "count"), add};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{3U, 0U, 4U, 0U}, BlueprintWire{5U, 0U, 6U, 0U},
                       BlueprintWire{6U, 0U, 4U, 1U}};
    const Running run = Run(blueprint, MakeEverything());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEvent("ready", {}));
    const auto advance = [&](const double seconds) {
        for (const std::string& id : run.host->AdvanceTimers(seconds)) {
            const Blueprint::Value args[] = {id};
            static_cast<void>(run.instance->RaiseEvent("timer", args));
        }
    };
    for (int frame = 0; frame < 10; ++frame) {
        advance(0.26);   // 2.6 s: five periods
    }
    EXPECT_EQ(run.Field("count"), Blueprint::Value{std::int64_t{5}});
    EXPECT_EQ(run.host->CallFunction("timers.clear", std::vector<Blueprint::Value>{std::string("Spawn")}), Blueprint::Value{true});
    advance(3.0);
    EXPECT_EQ(run.Field("count"), Blueprint::Value{std::int64_t{5}}) << "a cleared timer never fires";
}

TEST_F(BlueprintTest, ARetriggerableDelayRestartsItsWait) {
    BlueprintAsset blueprint;
    blueprint.variables = {{"done", BlueprintPinType::Int, "0", false}};
    BlueprintNode delay = Node(2U, "retriggerable_delay");
    delay.inputValues = {"", "1.0"};
    BlueprintNode set = Node(3U, "set_variable", "done");
    set.inputValues = {"", "1"};
    blueprint.nodes = {Node(4U, "event_custom"), delay, set};
    blueprint.wires = {BlueprintWire{4U, 0U, 2U, 0U}, BlueprintWire{2U, 0U, 3U, 0U}};
    const Running run = Run(blueprint, MakeEverything());
    ASSERT_NE(run.instance, nullptr);
    const Blueprint::Value poke[] = {std::string("MyEvent"), std::string()};
    const auto advance = [&](const double seconds) {
        for (const std::string& id : run.host->AdvanceTimers(seconds)) {
            const Blueprint::Value args[] = {id};
            static_cast<void>(run.instance->RaiseEvent("timer", args));
        }
    };
    ASSERT_TRUE(run.instance->RaiseEvent("custom_event", poke));
    advance(0.8);
    ASSERT_TRUE(run.instance->RaiseEvent("custom_event", poke));   // starts the second over
    advance(0.8);
    EXPECT_EQ(run.Field("done"), Blueprint::Value{std::int64_t{0}}) << "1.6 s, but never 1 s since the last poke";
    advance(0.3);
    EXPECT_EQ(run.Field("done"), Blueprint::Value{std::int64_t{1}});
}

TEST_F(BlueprintTest, ServicesSpawnDestroyTraceEmitAndKeepGameData) {
    Core::ScriptServices services;
    std::vector<Entity> destroyed;
    std::vector<std::pair<Entity, std::string>> sent;
    const Entity spawned = entityManager.CreateEntity();
    entityManager.AddComponent<TransformComponent>(spawned, TransformComponent{});
    std::string spawnedPath;
    services.spawn = [&](const std::string& path, const Blueprint::Vec3Value& at, const Blueprint::Vec3Value&) {
        spawnedPath = path;
        entityManager.GetComponent<TransformComponent>(spawned).localPosition =
            Math::Vector3{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
        return spawned;
    };
    services.destroy = [&](const Entity entity) { destroyed.push_back(entity); };
    services.emit = [&](const Entity target, const std::string& name, const std::string&) { sent.emplace_back(target, name); };
    services.trace = [&](const Blueprint::Vec3Value&, const Blueprint::Vec3Value& to, double, Entity) {
        Core::ScriptServices::TraceHit hit;
        hit.hit = true;
        hit.point = to;
        hit.normal = Blueprint::Vec3Value{0.0, 1.0, 0.0};
        hit.distance = 4.0;
        hit.entity = spawned;
        return hit;
    };

    // Ready: Spawn Entity at (1,2,3) -> Line Trace down -> Set Object Location (hit object, its hit + 1 up)
    //        -> Call Custom Event "Spawned" -> Destroy Object (the spawned one) -> Set Game Data "Score" = 7.5
    BlueprintAsset blueprint;
    blueprint.variables = {{"hitDistance", BlueprintPinType::Float, "0", false}};
    BlueprintNode spawn = Node(2U, "spawn_entity");
    spawn.inputValues = {"", "Entities/Crate.uventity", "1,2,3", "0,0,0"};
    BlueprintNode trace = Node(3U, "line_trace");
    trace.inputValues = {"", "0,10,0", "0,-10,0"};
    BlueprintNode call = Node(5U, "call_custom_event");
    call.inputValues = {"", "Spawned", ""};
    BlueprintNode score = Node(7U, "save_set_float");
    score.inputValues = {"", "Score", "7.5"};
    blueprint.nodes = {Node(1U, "event_ready"), spawn, trace, Node(4U, "set_variable", "hitDistance"), call,
                       Node(6U, "destroy_object"), score, Node(8U, "object_set_location")};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{2U, 0U, 3U, 0U}, BlueprintWire{3U, 0U, 4U, 0U},
                       BlueprintWire{3U, 5U, 4U, 1U},   // Distance
                       BlueprintWire{4U, 0U, 8U, 0U}, BlueprintWire{3U, 4U, 8U, 1U}, BlueprintWire{3U, 2U, 8U, 2U},
                       BlueprintWire{8U, 0U, 5U, 0U}, BlueprintWire{5U, 0U, 6U, 0U}, BlueprintWire{2U, 1U, 6U, 1U},
                       BlueprintWire{6U, 0U, 7U, 0U}};
    const Entity self = MakeEverything();
    const Running run = Run(blueprint, self, &services);
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEvent("ready", {})) << run.instance->GetLastError();
    EXPECT_EQ(spawnedPath, "Entities/Crate.uventity");
    EXPECT_EQ(run.Field("hitDistance"), Blueprint::Value{4.0});
    // Moved by Set Object Location to the trace's end.
    EXPECT_FLOAT_EQ(entityManager.GetComponent<TransformComponent>(spawned).localPosition.y, -10.0F);
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].first, self);
    EXPECT_EQ(sent[0].second, "Spawned");
    ASSERT_EQ(destroyed.size(), 1U);
    EXPECT_EQ(destroyed[0], spawned);
    EXPECT_EQ(services.gameData["Score"], "7.5");
    EXPECT_EQ(run.host->CallFunction("save.get_float", std::vector<Blueprint::Value>{std::string("Score"), 0.0}), Blueprint::Value{7.5});
    EXPECT_EQ(run.host->CallFunction("save.get_int", std::vector<Blueprint::Value>{std::string("Missing"), std::int64_t{4}}),
              Blueprint::Value{std::int64_t{4}});
}

TEST_F(BlueprintTest, ALifeSpanDestroysTheObjectThroughTheServices) {
    Core::ScriptServices services;
    std::vector<Entity> destroyed;
    services.destroy = [&](const Entity entity) { destroyed.push_back(entity); };
    const Entity self = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, self};
    host.SetServices(&services);
    EXPECT_EQ(host.CallFunction("world.set_life_span", std::vector<Blueprint::Value>{2.0}), Blueprint::Value{true});
    EXPECT_TRUE(host.AdvanceTimers(1.5).empty());
    EXPECT_TRUE(destroyed.empty());
    EXPECT_TRUE(host.AdvanceTimers(1.0).empty()) << "a life span raises no timer event";
    ASSERT_EQ(destroyed.size(), 1U);
    EXPECT_EQ(destroyed[0], self);
    // Scripts cannot touch the life span's own timer.
    EXPECT_EQ(host.CallFunction("timers.set", std::vector<Blueprint::Value>{std::string("__life_span"), 1.0, false}),
              Blueprint::Value{false});
}

TEST_F(BlueprintTest, ObjectsFindTheirTagsChildrenAndEachOther) {
    const Entity self = MakeEverything();
    const Entity enemy = entityManager.CreateEntity();
    entityManager.AddComponent<TransformComponent>(enemy, TransformComponent{});
    entityManager.AddComponent<NameComponent>(enemy, NameComponent{"Enemy_1"});
    GameplayTagComponent tags;
    tags.tags = {"Enemy"};
    entityManager.AddComponent<GameplayTagComponent>(enemy, tags);
    HierarchyComponent link;
    link.parent = enemy;
    const Entity child = entityManager.CreateEntity();
    entityManager.AddComponent<HierarchyComponent>(child, link);
    Core::BlueprintObjectHost host{entityManager, nullptr, self};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::Value> args) { return host.CallFunction(name, args); };
    const Blueprint::Value found = call("world.find_tag", {std::string("Enemy")});
    ASSERT_TRUE(std::holds_alternative<Blueprint::ObjectRef>(found));
    EXPECT_NE(std::get<Blueprint::ObjectRef>(found).id, 0U);
    EXPECT_EQ(call("object.name", {found}), Blueprint::Value{std::string("Enemy_1")});
    EXPECT_EQ(call("object.set_position", {found, Blueprint::Vec3Value{3.0, 0.0, 4.0}}), Blueprint::Value{true});
    EXPECT_EQ(call("object.distance", {call("world.self", {}), found}), Blueprint::Value{5.0});
    const Blueprint::Value children = call("object.children", {found});
    const auto& list = std::get<std::shared_ptr<const Blueprint::CollectionValue>>(children);
    ASSERT_EQ(list->items.size(), 1U);
    EXPECT_EQ(call("object.parent", {list->items[0]}), found);
    EXPECT_EQ(call("world.find_tag", {std::string("Nobody")}), Blueprint::Value{Blueprint::ObjectRef{}});
    EXPECT_EQ(call("world.is_valid", {Blueprint::Value{Blueprint::ObjectRef{}}}), Blueprint::Value{false});
    // Without services the world calls fail closed.
    EXPECT_EQ(call("world.spawn", {std::string("x.uventity"), Blueprint::Vec3Value{}, Blueprint::Vec3Value{}}),
              Blueprint::Value{Blueprint::ObjectRef{}});
    EXPECT_EQ(call("game.quit", {}), Blueprint::Value{false});
    EXPECT_EQ(call("math.select_string", {false, std::string("A"), std::string("B")}), Blueprint::Value{std::string("B")});
}

TEST_F(BlueprintTest, EffectsLookClipsAndWalkingReachTheirComponents) {
    Core::ScriptServices services;
    std::int64_t burst = 0;
    std::string sound;
    double shake = 0.0;
    services.burstParticles = [&](const Entity, const std::int64_t count) {
        burst += count;
        return true;
    };
    services.playSound = [&](const std::string& path, const Blueprint::Vec3Value&, double, const bool spatial) {
        sound = path + (spatial ? "@3d" : "@2d");
        return true;
    };
    services.cameraShake = [&](const double strength, double) { shake = strength; };
    const Entity self = MakeEverything();
    Core::BlueprintObjectHost host{entityManager, nullptr, self};
    host.SetServices(&services);
    const auto call = [&](const std::string_view name, std::vector<Blueprint::Value> args) { return host.CallFunction(name, args); };
    EXPECT_EQ(call("particles.set_active", {false}), Blueprint::Value{true});
    EXPECT_FALSE(entityManager.GetComponent<ParticleEmitterComponent>(self).emitting);
    EXPECT_EQ(call("particles.burst", {std::int64_t{25}}), Blueprint::Value{true});
    EXPECT_EQ(burst, 25);
    EXPECT_EQ(call("audio.play_at", {std::string("Sounds/Hit.wav"), Blueprint::Vec3Value{1.0, 0.0, 0.0}, 1.0}), Blueprint::Value{true});
    EXPECT_EQ(sound, "Sounds/Hit.wav@3d");
    EXPECT_EQ(call("camera.shake", {0.5, 1.0}), Blueprint::Value{true});
    EXPECT_DOUBLE_EQ(shake, 0.5);
    EXPECT_EQ(call("render.set_material", {std::string("Materials/Red.uvmat")}), Blueprint::Value{true});
    EXPECT_EQ(entityManager.GetComponent<SurfaceInstanceComponent>(self).materialOverridePath, "Materials/Red.uvmat");
    EXPECT_EQ(call("render.set_transparency", {2.0}), Blueprint::Value{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<SurfaceInstanceComponent>(self).transparency, 1.0F) << "clamped";
    // Without the game's files a clip is played by the one it has.
    host.SetServices(nullptr);
    EXPECT_EQ(call("sequencer.play", {std::string()}), Blueprint::Value{true});
    EXPECT_TRUE(entityManager.GetComponent<AnimationSequencerComponent>(self).playRequested);
    EXPECT_EQ(call("sequencer.is_playing", {}), Blueprint::Value{true});
    EXPECT_EQ(call("sequencer.set_rate", {-1.0}), Blueprint::Value{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<AnimationSequencerComponent>(self).speed, -1.0F);
    EXPECT_EQ(call("sequencer.stop", {}), Blueprint::Value{true});
    EXPECT_EQ(call("sequencer.is_playing", {}), Blueprint::Value{false});
    EXPECT_EQ(call("ai.move_to", {Blueprint::Vec3Value{4.0, 0.0, 2.0}}), Blueprint::Value{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<NavSeeker3DComponent>(self).targetPosition.x, 4.0F);
    EXPECT_EQ(call("ai.status", {}), Blueprint::Value{std::string("Idle")});
    EXPECT_EQ(call("camera.shake", {0.5, 1.0}), Blueprint::Value{false}) << "no services: fails closed";
}

TEST_F(BlueprintTest, ForEachWalksASplitTextIntoAnArray) {
    // Ready -> For Each (String) over Split("red,green,blue", ",") -> Add the item to `seen`, and the
    // index into `last`; Completed -> Set `label` = Format Text("{0} of {1}", Length(seen), "3").
    BlueprintAsset blueprint;
    blueprint.variables = {{"seen", BlueprintPinType::StringArray, "", false},
                           {"last", BlueprintPinType::Int, "-1", false},
                           {"label", BlueprintPinType::String, "", false}};
    BlueprintNode split = Node(3U, "string_split");
    split.inputValues = {"red,green,blue", ","};
    BlueprintNode format = Node(9U, "format_text");
    format.inputValues = {"{0} of {1}", "", "3"};
    blueprint.nodes = {Node(1U, "event_ready"), Node(2U, "for_each_String"), split, Node(4U, "array_add_String"),
                       Node(5U, "get_variable", "seen"), Node(6U, "set_variable", "last"), Node(7U, "set_variable", "label"),
                       Node(8U, "array_length_String"), format, Node(10U, "int_to_string"), Node(11U, "get_variable", "seen")};
    blueprint.wires = {BlueprintWire{1U, 0U, 2U, 0U}, BlueprintWire{3U, 0U, 2U, 1U},
                       // body: Add(seen, Element) -> last = Index
                       BlueprintWire{2U, 0U, 4U, 0U}, BlueprintWire{5U, 0U, 4U, 1U}, BlueprintWire{2U, 1U, 4U, 2U},
                       BlueprintWire{4U, 0U, 6U, 0U}, BlueprintWire{2U, 2U, 6U, 1U},
                       // completed: label = Format(str(Length(seen)))
                       BlueprintWire{2U, 3U, 7U, 0U}, BlueprintWire{11U, 0U, 8U, 0U}, BlueprintWire{8U, 0U, 10U, 0U},
                       BlueprintWire{10U, 0U, 9U, 1U}, BlueprintWire{9U, 0U, 7U, 1U}};
    const Running run = Run(blueprint, MakeEverything());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEvent("ready", {})) << run.instance->GetLastError();
    EXPECT_EQ(run.Field("last"), Blueprint::Value{std::int64_t{2}});
    EXPECT_EQ(run.Field("label"), Blueprint::Value{std::string("3 of 3")});
    // Kept by value: Field returns a copy, and a reference into it would dangle.
    const Blueprint::Value seenValue = run.Field("seen");
    const auto seen = std::get<std::shared_ptr<const Blueprint::CollectionValue>>(seenValue);
    ASSERT_EQ(seen->items.size(), 3U);
    EXPECT_EQ(seen->items[1], Blueprint::Value{std::string("green")});

    Core::BlueprintObjectHost host{entityManager, nullptr, MakeEverything()};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::Value> args) { return host.CallFunction(name, args); };
    EXPECT_EQ(call("math.wrap", {370.0, 0.0, 360.0}), Blueprint::Value{10.0});
    EXPECT_EQ(call("math.wrap", {-30.0, 0.0, 360.0}), Blueprint::Value{330.0});
    EXPECT_EQ(call("math.snap", {2.6, 0.5}), Blueprint::Value{2.5});
    EXPECT_EQ(call("math.round", {2.5}), Blueprint::Value{std::int64_t{3}});
    EXPECT_EQ(call("text.to_int", {std::string("x")}), Blueprint::Value{std::int64_t{0}}) << "not a number reads 0";
    EXPECT_EQ(call("text.replace", {std::string("a-b-c"), std::string("-"), std::string("--")}), Blueprint::Value{std::string("a--b--c")});
    EXPECT_EQ(call("text.substring", {std::string("hello"), std::int64_t{1}, std::int64_t{99}}), Blueprint::Value{std::string("ello")});
    const Blueprint::Value turned = call("math.rotate_about", {Blueprint::Vec3Value{1.0, 0.0, 0.0}, Blueprint::Vec3Value{0.0, 1.0, 0.0}, 90.0});
    EXPECT_NEAR(std::get<Blueprint::Vec3Value>(turned).z, -1.0, 1e-5);
}

TEST_F(BlueprintTest, WidgetNodesMoveSizeFadeAndAnimateWidgets) {
    const Entity button = entityManager.CreateEntity();
    entityManager.AddComponent<NameComponent>(button, NameComponent{"Play"});
    entityManager.AddComponent<UIButtonComponent>(button, UIButtonComponent{});
    UITweenComponent tween;
    tween.playing = false;
    tween.elapsed = 0.7F;
    entityManager.AddComponent<UITweenComponent>(button, tween);
    const Entity label = entityManager.CreateEntity();
    entityManager.AddComponent<NameComponent>(label, NameComponent{"Title"});
    entityManager.AddComponent<UITextComponent>(label, UITextComponent{});
    Core::BlueprintObjectHost host{entityManager, nullptr, MakeEverything()};
    const auto call = [&](const std::string_view name, std::vector<Blueprint::Value> args) { return host.CallFunction(name, args); };
    EXPECT_EQ(call("ui.set_position", {std::string("Play"), Blueprint::Vec3Value{40.0, 60.0, 0.0}}), Blueprint::Value{true});
    EXPECT_EQ(call("ui.set_size", {std::string("Play"), Blueprint::Vec3Value{200.0, 50.0, 0.0}}), Blueprint::Value{true});
    const UIButtonComponent& b = entityManager.GetComponent<UIButtonComponent>(button);
    EXPECT_FLOAT_EQ(b.rect.position.y, 60.0F);
    EXPECT_FLOAT_EQ(b.rect.size.x, 200.0F);
    EXPECT_EQ(call("ui.get_size", {std::string("Play")}), (Blueprint::Value{Blueprint::Vec3Value{200.0, 50.0, 0.0}}));
    EXPECT_EQ(call("ui.set_size", {std::string("Play"), Blueprint::Vec3Value{-1.0, 5.0, 0.0}}), Blueprint::Value{false}) << "no negative size";
    EXPECT_EQ(call("ui.set_position", {std::string("Title"), Blueprint::Vec3Value{5.0, 6.0, 0.0}}), Blueprint::Value{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<UITextComponent>(label).positionPixels.x, 5.0F);
    EXPECT_EQ(call("ui.set_opacity", {std::string("Title"), 0.25}), Blueprint::Value{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<UITextComponent>(label).alpha, 0.25F);
    EXPECT_EQ(call("ui.set_font_size", {std::string("Title"), 40.0}), Blueprint::Value{true});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<UITextComponent>(label).fontSize, 40.0F);
    EXPECT_EQ(call("ui.play_tween", {std::string("Play")}), Blueprint::Value{true});
    EXPECT_TRUE(entityManager.GetComponent<UITweenComponent>(button).playing);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<UITweenComponent>(button).elapsed, 0.0F);
    EXPECT_EQ(call("ui.is_hovered", {std::string("Play")}), Blueprint::Value{false});
    EXPECT_EQ(call("ui.set_opacity", {std::string("Nobody"), 0.5}), Blueprint::Value{false});
}

TEST_F(BlueprintTest, NetworkingNodesDelegateToEngineRuntimeNetworking) {
    // Verify the Networking nodes defined in Engine/Editor/EditorCore/Blueprint/Nodes/Networking/ are registered in
    // the Blueprint catalog, translate cleanly, and execute against BlueprintObjectHost
    // (which delegates directly to Engine/Runtime/Networking/).
    const Asset::BlueprintNodeType* const relayCount = Asset::FindBlueprintNodeType("net_relay_peer_count");
    const Asset::BlueprintNodeType* const lobbyRoom = Asset::FindBlueprintNodeType("net_lobby_room");
    const Asset::BlueprintNodeType* const rpcBegin = Asset::FindBlueprintNodeType("net_rpc_begin");
    const Asset::BlueprintNodeType* const rpcSend = Asset::FindBlueprintNodeType("net_rpc_send");
    ASSERT_NE(relayCount, nullptr);
    ASSERT_NE(lobbyRoom, nullptr);
    ASSERT_NE(rpcBegin, nullptr);
    ASSERT_NE(rpcSend, nullptr);
    EXPECT_EQ(relayCount->category, "Networking|Relay");
    EXPECT_EQ(lobbyRoom->category, "Networking|Lobby");
    EXPECT_EQ(rpcBegin->category, "Networking|RPC");

    BlueprintAsset asset;
    asset.variables.push_back(Asset::BlueprintVariable{"peers", BlueprintPinType::Int, "99", false});
    asset.variables.push_back(Asset::BlueprintVariable{"room", BlueprintPinType::Int, "99", false});
    asset.nodes = {
        Node(1U, "event_ready"),
        Node(2U, "net_relay_peer_count"),
        Node(3U, "set_variable", "peers"),
        Node(4U, "net_lobby_room"),
        Node(5U, "set_variable", "room"),
    };
    asset.wires = {
        BlueprintWire{1U, 0U, 3U, 0U},
        BlueprintWire{2U, 0U, 3U, 1U},
        BlueprintWire{3U, 0U, 5U, 0U},
        BlueprintWire{4U, 0U, 5U, 1U},
    };

    {
        const Entity self = MakeEverything();
        Core::BlueprintObjectHost host{entityManager, nullptr, self};
        const Asset::BlueprintCompilation compiled = Asset::CompileBlueprintGraph(asset, "BP_Net", "Object3D", host);
        ASSERT_TRUE(compiled.problems.empty());
        ASSERT_TRUE(compiled.result.IsSuccess());
    }

    const Running run = Run(asset, MakeEverything());
    ASSERT_NE(run.instance, nullptr);
    ASSERT_TRUE(run.instance->RaiseEvent("ready"));
    // With no attached relay/lobby peer, Engine/Runtime/Networking/ host bindings fail closed to 0.
    EXPECT_EQ(run.Field("peers"), Blueprint::Value{std::int64_t{0}});
    EXPECT_EQ(run.Field("room"), Blueprint::Value{std::int64_t{0}});
}

} // namespace
} // namespace UVE::Scene::Tests
