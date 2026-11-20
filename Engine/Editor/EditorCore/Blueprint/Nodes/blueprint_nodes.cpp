// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_nodes.h"
#include "blueprint_networking_nodes.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace UVE::Asset {
namespace {

using T = BlueprintPinTypeUVE;
using Role = BlueprintNodeRoleUVE;
using Pin = BlueprintPinSpecUVE;

[[nodiscard]] Pin Exec(std::string name = "") { return Pin{std::move(name), T::Exec, ""}; }

/// Where each node belongs: an object's graph or a widget's, and the component it needs.
void AssignScopesUVE(std::vector<BlueprintNodeTypeUVE>& all) {
    constexpr auto object = static_cast<std::uint8_t>(BlueprintGraphKindUVE::Object);
    constexpr auto widget = static_cast<std::uint8_t>(BlueprintGraphKindUVE::Widget);
    for (BlueprintNodeTypeUVE& n : all) {
        const std::string& c = n.category;
        if (n.event == "custom_event" || n.event == "timer" || c.starts_with("Utilities") || c == "Game" || c.starts_with("Game|")) {
            n.graphs = object | widget;
        } else if (c.starts_with("User Interface")) {
            // A widget's own controls; creating a widget or the mouse cursor work from any graph.
            const bool anywhere = n.key == "ui_create_widget" || n.key == "ui_add_to_viewport" || n.key == "ui_show_mouse_cursor";
            n.graphs = anywhere ? (object | widget) : widget;
        } else if (c == "Character" || c == "Physics" || c.starts_with("Physics|") || c == "World" || c == "Effects" || c == "AI" ||
                   c.starts_with("Animation|") || c.starts_with("Rendering|") || c == "Animation" || c == "Audio" || c == "Transform" || c == "Rendering" ||
                   c.starts_with("Gameplay") ||
                   (c == "Events" && n.event != "ready" && n.event != "tick")) {
            n.graphs = object;
        }
        if (c == "Character" || n.key == "is_grounded") {
            n.needs = "CharacterController";
        } else if (n.key.starts_with("apply_")) {
            n.needs = "Rigid3D";
        } else if (c == "Audio") {
            n.needs = "AudioSource";
        } else if (c == "Effects") {
            n.needs = "ParticleEmitter";
        } else if (c == "Rendering|Material") {
            n.needs = "SurfaceInstance";
        } else if (c == "Animation|Sequencer") {
            n.needs = "AnimationSequencer";
        } else if (c == "AI") {
            n.needs = "NavSeeker3D";
        } else if (n.key.find("light") != std::string::npos) {
            n.needs = "Light";
        } else if (n.key.find("field_of_view") != std::string::npos) {
            n.needs = "Camera";
        } else if (n.key.starts_with("attr_")) {
            n.needs = "GameplayAttributes";
        } else if (n.key.starts_with("tag_")) {
            n.needs = "GameplayTag";
        }
    }
}

[[nodiscard]] std::vector<BlueprintNodeTypeUVE> BuildCatalogueUVE() {
    std::vector<BlueprintNodeTypeUVE> all;
    const auto event = [&all](std::string key, std::string title, std::string ev, std::vector<Pin> data, std::string keywords,
                              std::string tip) {
        BlueprintNodeTypeUVE n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = "Events";
        n.keywords = std::move(keywords);
        n.role = Role::Event;
        n.event = std::move(ev);
        n.outputs.push_back(Exec());
        for (Pin& p : data) {
            n.outputs.push_back(std::move(p));
        }
        n.tooltip = std::move(tip);
        all.push_back(std::move(n));
    };
    const auto action = [&all](std::string key, std::string title, std::string category, std::vector<Pin> data, std::string code,
                               std::string keywords = "", std::string tip = "") {
        BlueprintNodeTypeUVE n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Action;
        n.inputs.push_back(Exec());
        for (Pin& p : data) {
            n.inputs.push_back(std::move(p));
        }
        n.outputs.push_back(Exec());
        n.code = std::move(code);
        n.tooltip = std::move(tip);
        all.push_back(std::move(n));
    };
    const auto pure = [&all](std::string key, std::string title, std::string category, std::vector<Pin> inputs, std::vector<Pin> outputs,
                             std::vector<std::string> codes, std::string keywords = "", std::string tip = "") {
        BlueprintNodeTypeUVE n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Pure;
        n.inputs = std::move(inputs);
        n.outputs = std::move(outputs);
        n.outputCode = std::move(codes);
        n.tooltip = std::move(tip);
        all.push_back(std::move(n));
    };
    const auto flow = [&all](std::string key, std::string title, Role role, std::vector<Pin> inputs, std::vector<Pin> outputs,
                             std::string keywords, std::string tip) {
        BlueprintNodeTypeUVE n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = "Flow Control";
        n.keywords = std::move(keywords);
        n.role = role;
        n.inputs = std::move(inputs);
        n.outputs = std::move(outputs);
        n.tooltip = std::move(tip);
        all.push_back(std::move(n));
    };

    // ---- events
    event("event_ready", "Event Ready", "ready", {}, "beginplay begin play start", "Once, when the object starts");
    event("event_tick", "Event Tick", "tick", {Pin{"Delta Seconds", T::Float, ""}}, "update frame", "Every frame");
    event("event_collision_enter", "On Collision Enter", "collision_enter", {Pin{"Other", T::String, ""}}, "hit contact", "Started touching another body");
    event("event_collision_exit", "On Collision Exit", "collision_exit", {Pin{"Other", T::String, ""}}, "hit contact", "Stopped touching another body");
    event("event_overlap_enter", "On Overlap Enter", "overlap_enter", {Pin{"Other", T::String, ""}}, "trigger begin overlap", "Something entered");
    event("event_overlap_exit", "On Overlap Exit", "overlap_exit", {Pin{"Other", T::String, ""}}, "trigger end overlap", "Something left");
    event("event_animation", "On Animation Event", "animation_event", {Pin{"Event", T::String, ""}}, "notify footstep clip", "A clip passed one of its events");
    event("event_montage_notify", "On Montage Notify", "montage_notify", {Pin{"Notify", T::String, ""}}, "anim notify", "A montage passed a notify");
    event("event_montage_notify_begin", "On Montage Notify Begin", "montage_notify_begin", {Pin{"Notify", T::String, ""}}, "notify state", "A montage entered a notify state");
    event("event_montage_notify_end", "On Montage Notify End", "montage_notify_end", {Pin{"Notify", T::String, ""}}, "notify state", "A montage left a notify state");
    event("event_montage_ended", "On Montage Ended", "montage_ended", {Pin{"Montage", T::String, ""}}, "finished", "A montage ended");

    // Named events: the node names which one (empty: any), like a widget event.
    const auto namedEvent = [&all](std::string key, std::string title, std::string category, std::string ev, std::string field,
                                   std::string fieldDefault, std::vector<Pin> data, std::string keywords, std::string tip) {
        BlueprintNodeTypeUVE n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Event;
        n.event = std::move(ev);
        n.inputs.push_back(Pin{std::move(field), T::String, std::move(fieldDefault)});
        n.outputs.push_back(Exec());
        for (Pin& p : data) {
            n.outputs.push_back(std::move(p));
        }
        n.tooltip = std::move(tip);
        all.push_back(std::move(n));
    };
    namedEvent("event_custom", "Custom Event", "Events", "custom_event", "Event", "MyEvent",
               {Pin{"Event", T::String, ""}, Pin{"Payload", T::String, ""}}, "custom signal message dispatcher broadcast",
               "Runs when Call Custom Event names it (here, or from another object)");
    namedEvent("event_timer", "On Timer", "Events", "timer", "Timer", "Spawn", {Pin{"Timer", T::String, ""}}, "timer elapsed interval repeat",
               "Runs when the timer Set Timer started (by its name) runs out");
    event("event_destroyed", "On Destroyed", "destroyed", {}, "end play removed killed", "Just before the object is removed");

    // ---- flow control
    flow("branch", "Branch", Role::Branch, {Exec(), Pin{"Condition", T::Bool, "false"}}, {Exec("True"), Exec("False")}, "if else", "One way or the other");
    flow("sequence", "Sequence", Role::Sequence, {Exec()}, {Exec("Then 0"), Exec("Then 1"), Exec("Then 2"), Exec("Then 3")}, "then order", "Each output in turn");
    flow("for_loop", "For Loop", Role::ForLoop, {Exec(), Pin{"First Index", T::Int, "0"}, Pin{"Last Index", T::Int, "9"}},
         {Exec("Loop Body"), Pin{"Index", T::Int, ""}, Exec("Completed")}, "repeat count", "From First to Last, inclusive");
    flow("while_loop", "While Loop", Role::While, {Exec(), Pin{"Condition", T::Bool, "false"}}, {Exec("Loop Body"), Exec("Completed")}, "repeat", "While the condition holds");
    flow("do_once", "Do Once", Role::DoOnce, {Exec()}, {Exec("Completed")}, "single first", "Only the first time");
    flow("flip_flop", "Flip Flop", Role::FlipFlop, {Exec()}, {Exec("A"), Exec("B"), Pin{"Is A", T::Bool, ""}}, "toggle alternate", "A, then B, then A ...");
    flow("delay", "Delay", Role::Delay, {Exec(), Pin{"Duration", T::Float, "0.5"}}, {Exec("Completed")}, "wait timer seconds", "Waits, then goes on");
    flow("gate", "Gate", Role::Gate, {Exec("Enter"), Exec("Open"), Exec("Close"), Exec("Toggle"), Pin{"Start Closed", T::Bool, "false"}},
         {Exec("Exit")}, "open close door block", "Enter goes on only while the gate is open");
    flow("multi_gate", "Multi Gate", Role::MultiGate, {Exec("Enter"), Exec("Reset"), Pin{"Loop", T::Bool, "true"}},
         {Exec("Out 0"), Exec("Out 1"), Exec("Out 2"), Exec("Out 3")}, "cycle round robin each next", "Each time, the next output");
    flow("do_n", "Do N", Role::DoN, {Exec("Enter"), Pin{"N", T::Int, "3"}, Exec("Reset")}, {Exec("Exit"), Pin{"Counter", T::Int, ""}},
         "limit times count", "Goes on the first N times, until Reset");
    flow("retriggerable_delay", "Retriggerable Delay", Role::RetriggerableDelay, {Exec(), Pin{"Duration", T::Float, "0.5"}}, {Exec("Completed")},
         "wait debounce restart timer", "Waits; entering again starts the wait over");
    flow("switch_int", "Switch on Int", Role::SwitchInt, {Exec(), Pin{"Selection", T::Int, "0"}},
         {Exec("0"), Exec("1"), Exec("2"), Exec("3"), Exec("Default")}, "case select index", "The output of the number");
    flow("switch_string", "Switch on String", Role::SwitchString,
         {Exec(), Pin{"Selection", T::String, ""}, Pin{"Case 0", T::String, "A"}, Pin{"Case 1", T::String, "B"}, Pin{"Case 2", T::String, "C"},
          Pin{"Case 3", T::String, "D"}},
         {Exec("Case 0"), Exec("Case 1"), Exec("Case 2"), Exec("Case 3"), Exec("Default")}, "case select name", "The output of the matching case");
    flow("is_valid", "Is Valid", Role::IsValid, {Exec(), Pin{"Object", T::Object, ""}}, {Exec("Is Valid"), Exec("Is Not Valid")}, "none null exists", "Whether the object was found");

    // ---- utilities
    action("print", "Print String", "Utilities", {Pin{"Text", T::String, "Hello"}}, "print({1})", "log debug message", "Writes to the Output Log");
    pure("to_string_float", "Float To String", "Utilities|Conversion", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    pure("to_string_int", "Int To String", "Utilities|Conversion", {Pin{"Value", T::Int, "0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    pure("to_string_bool", "Bool To String", "Utilities|Conversion", {Pin{"Value", T::Bool, "false"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    pure("to_string_vector", "Vector To String", "Utilities|Conversion", {Pin{"Value", T::Vector, "0,0,0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"});
    pure("int_to_float", "Int To Float", "Utilities|Conversion", {Pin{"Value", T::Int, "0"}}, {Pin{"Value", T::Float, ""}}, {"float({0})"});
    pure("float_to_int", "Float To Int", "Utilities|Conversion", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Value", T::Int, ""}}, {"int({0})"});
    pure("append_string", "Append", "Utilities|String", {Pin{"A", T::String, ""}, Pin{"B", T::String, ""}}, {Pin{"Text", T::String, ""}}, {"{0} + {1}"}, "concat join");

    // ---- transform
    pure("get_position", "Get Position", "Transform", {}, {Pin{"Position", T::Vector, ""}}, {"position"}, "location actor");
    action("set_position", "Set Position", "Transform", {Pin{"Position", T::Vector, "0,0,0"}}, "position = {1}", "location teleport move");
    action("add_position", "Add Offset", "Transform", {Pin{"Offset", T::Vector, "0,0,0"}}, "position += {1}", "move translate");
    pure("get_scale", "Get Scale", "Transform", {}, {Pin{"Scale", T::Vector, ""}}, {"scale"});
    action("set_scale", "Set Scale", "Transform", {Pin{"Scale", T::Vector, "1,1,1"}}, "scale = {1}", "size");
    pure("get_velocity", "Get Velocity", "Transform", {}, {Pin{"Velocity", T::Vector, ""}}, {"velocity"}, "speed", "A character or rigid body's velocity");
    action("set_velocity", "Set Velocity", "Transform", {Pin{"Velocity", T::Vector, "0,0,0"}}, "velocity = {1}", "speed move", "A character or rigid body's velocity");
    pure("get_visible", "Is Visible", "Transform", {}, {Pin{"Visible", T::Bool, ""}}, {"visible"});
    action("set_visible", "Set Visible", "Transform", {Pin{"Visible", T::Bool, "true"}}, "visible = {1}", "hide show");
    pure("get_name", "Get Name", "Transform", {}, {Pin{"Name", T::String, ""}}, {"name"}, "self");

    // ---- physics
    action("apply_force", "Apply Force", "Physics", {Pin{"Force", T::Vector, "0,0,0"}}, "physics.apply_force({1})", "add push", "A rigid body");
    action("apply_impulse", "Apply Impulse", "Physics", {Pin{"Impulse", T::Vector, "0,0,0"}}, "physics.apply_impulse({1})", "add kick", "A rigid body");
    action("apply_torque", "Apply Torque", "Physics", {Pin{"Torque", T::Vector, "0,0,0"}}, "physics.apply_torque({1})", "spin rotate", "A rigid body");
    pure("is_grounded", "Is Grounded", "Physics", {}, {Pin{"Grounded", T::Bool, ""}}, {"grounded"}, "on floor falling", "A character");

    // ---- input
    pure("input_pressed", "Is Action Pressed", "Input", {Pin{"Action", T::String, "Jump"}}, {Pin{"Pressed", T::Bool, ""}}, {"input.pressed({0})"}, "key button just");
    pure("input_held", "Is Action Held", "Input", {Pin{"Action", T::String, "Jump"}}, {Pin{"Held", T::Bool, ""}}, {"input.held({0})"}, "key button down");
    pure("input_released", "Was Action Released", "Input", {Pin{"Action", T::String, "Jump"}}, {Pin{"Released", T::Bool, ""}}, {"input.released({0})"}, "key button up");
    pure("input_axis", "Get Axis", "Input", {Pin{"Negative", T::String, "left"}, Pin{"Positive", T::String, "right"}}, {Pin{"Value", T::Float, ""}},
         {"input.axis({0}, {1})"}, "move stick");

    pure("input_axis_value", "Get Axis Value", "Input", {Pin{"Action", T::String, "MoveForward"}}, {Pin{"Value", T::Float, ""}},
         {"input.axis_value({0})"}, "move stick wasd forward right axis1d", "An Axis action from the Input Map (MoveForward, MoveHorizontal, LookHorizontal ...): -1 to 1");
    pure("input_mouse_delta", "Get Mouse Delta", "Input", {}, {Pin{"X", T::Float, ""}, Pin{"Y", T::Float, ""}},
         {"input.mouse_delta().x", "input.mouse_delta().y"}, "look turn camera mouse", "How far the mouse moved this frame, in pixels");

    // ---- character (a Character Controller)
    action("add_movement_input", "Add Movement Input", "Character", {Pin{"World Direction", T::Vector, "0,0,-1"}, Pin{"Scale", T::Float, "1.0"}},
           "character.move({1} * {2})", "walk run wasd move", "Steers the character this frame (with its speed, acceleration and air control)");
    action("character_jump", "Jump", "Character", {}, "character.jump()", "hop leap", "Jumps when it can (on the floor, or within coyote time)");
    action("character_launch", "Launch Character", "Character", {Pin{"Velocity", T::Vector, "0,10,0"}}, "character.launch({1})", "knockback push",
           "Adds to its velocity at once");
    pure("character_is_falling", "Is Falling", "Character", {}, {Pin{"Falling", T::Bool, ""}}, {"character.falling()"}, "air midair");
    pure("character_speed", "Get Speed", "Character", {}, {Pin{"Speed", T::Float, ""}}, {"character.speed()"}, "velocity ground",
         "Its speed along the ground, metres per second");
    action("set_max_walk_speed", "Set Max Walk Speed", "Character", {Pin{"Speed", T::Float, "5.0"}}, "max_speed = {1}", "run sprint");
    pure("get_max_walk_speed", "Get Max Walk Speed", "Character", {}, {Pin{"Speed", T::Float, ""}}, {"max_speed"});
    action("set_jump_height", "Set Jump Height", "Character", {Pin{"Height", T::Float, "1.5"}}, "jump_height = {1}");

    // ---- transform: rotation and directions
    pure("get_rotation", "Get Rotation", "Transform", {}, {Pin{"Rotation", T::Vector, ""}}, {"rotation"}, "euler angles", "Degrees: X pitch, Y yaw, Z roll");
    action("set_rotation", "Set Rotation", "Transform", {Pin{"Rotation", T::Vector, "0,0,0"}}, "rotation = {1}", "euler angles turn");
    action("add_yaw", "Add Yaw", "Transform", {Pin{"Degrees", T::Float, "0.0"}}, "transform.add_yaw({1})", "turn look horizontal", "Turns about the world's up");
    action("add_pitch", "Add Pitch", "Transform", {Pin{"Degrees", T::Float, "0.0"}}, "transform.add_pitch({1})", "look vertical tilt");
    action("look_at", "Look At", "Transform", {Pin{"Target", T::Vector, "0,0,0"}}, "transform.look_at({1})", "face aim", "Turns so it faces the point");
    pure("get_forward_vector", "Get Forward Vector", "Transform", {}, {Pin{"Forward", T::Vector, ""}}, {"transform.forward()"}, "direction facing");
    pure("get_right_vector", "Get Right Vector", "Transform", {}, {Pin{"Right", T::Vector, ""}}, {"transform.right()"}, "direction side");
    pure("get_up_vector", "Get Up Vector", "Transform", {}, {Pin{"Up", T::Vector, ""}}, {"transform.up()"}, "direction");
    pure("get_distance_to", "Get Distance To", "Transform", {Pin{"Target", T::Vector, "0,0,0"}}, {Pin{"Distance", T::Float, ""}},
         {"transform.distance_to({0})"}, "range far near");

    // ---- rendering: light and camera
    action("set_light_intensity", "Set Light Intensity", "Rendering", {Pin{"Intensity", T::Float, "1.0"}}, "intensity = {1}", "brightness energy");
    pure("get_light_intensity", "Get Light Intensity", "Rendering", {}, {Pin{"Intensity", T::Float, ""}}, {"intensity"});
    action("set_light_color", "Set Light Color", "Rendering", {Pin{"Color", T::Vector, "1,1,1"}}, "light_color = {1}", "colour tint");
    action("set_field_of_view", "Set Field Of View", "Rendering", {Pin{"Degrees", T::Float, "60.0"}}, "fov = {1}", "zoom camera fov");
    pure("get_field_of_view", "Get Field Of View", "Rendering", {}, {Pin{"Degrees", T::Float, ""}}, {"fov"}, "zoom camera fov");

    // ---- audio
    action("audio_play", "Play Sound", "Audio", {}, "audio.play()", "sound sfx", "Its AudioSource");
    action("audio_stop", "Stop Sound", "Audio", {}, "audio.stop()", "sound sfx", "Its AudioSource");
    pure("audio_is_playing", "Is Sound Playing", "Audio", {}, {Pin{"Playing", T::Bool, ""}}, {"audio.is_playing()"});
    action("set_volume", "Set Volume", "Audio", {Pin{"Volume", T::Float, "1.0"}}, "volume = {1}");
    action("set_pitch", "Set Pitch", "Audio", {Pin{"Pitch", T::Float, "1.0"}}, "pitch = {1}");

    // ---- animation
    action("montage_play", "Play Montage", "Animation", {Pin{"Montage", T::String, "Attack"}}, "montage.play({1})", "attack anim", "On its MontageSequencer, by the montage's name");
    action("montage_play_section", "Play Montage Section", "Animation", {Pin{"Montage", T::String, "Attack"}, Pin{"Section", T::String, "Default"}},
           "montage.play_section({1}, {2})", "combo");
    action("montage_jump_to", "Jump To Section", "Animation", {Pin{"Section", T::String, "Default"}}, "montage.jump_to({1})", "combo next");
    action("montage_stop", "Stop Montage", "Animation", {}, "montage.stop()");
    pure("montage_is_playing", "Is Montage Playing", "Animation", {}, {Pin{"Playing", T::Bool, ""}}, {"montage.is_playing()"});
    pure("montage_section", "Current Section", "Animation", {}, {Pin{"Section", T::String, ""}}, {"montage.section()"});
    pure("montage_curve", "Montage Curve", "Animation", {Pin{"Curve", T::String, ""}}, {Pin{"Value", T::Float, ""}}, {"montage.curve({0})"});
    // Animation Graph parameters, by name, on its Animation Graph (or the one under it).
    action("anim_set_float", "Set Anim Float", "Animation", {Pin{"Parameter", T::String, "Speed"}, Pin{"Value", T::Float, "0.0"}},
           "anim.set_float({1}, {2})", "animation graph parameter", "A Float parameter of its Animation Graph");
    action("anim_set_bool", "Set Anim Bool", "Animation", {Pin{"Parameter", T::String, "IsJumping"}, Pin{"Value", T::Bool, "true"}},
           "anim.set_bool({1}, {2})", "animation graph parameter", "A Bool parameter of its Animation Graph");
    action("anim_set_int", "Set Anim Int", "Animation", {Pin{"Parameter", T::String, "Weapon"}, Pin{"Value", T::Int, "0"}},
           "anim.set_int({1}, {2})", "animation graph parameter", "A whole-number parameter (kept as a Float)");
    action("anim_trigger", "Fire Anim Trigger", "Animation", {Pin{"Parameter", T::String, "Jump"}}, "anim.trigger({1})",
           "animation graph trigger", "A Trigger parameter: set until a transition uses it");
    action("anim_set_vector", "Set Anim Vector", "Animation",
           {Pin{"Parameter", T::String, "LookAt"}, Pin{"X", T::Float, "0.0"}, Pin{"Y", T::Float, "0.0"}, Pin{"Z", T::Float, "0.0"}},
           "anim.set_vector({1}, {2}, {3}, {4})", "animation graph ik target", "A Vector3 parameter (an IK or look target)");
    pure("anim_get_float", "Get Anim Float", "Animation", {Pin{"Parameter", T::String, "Speed"}}, {Pin{"Value", T::Float, ""}},
         {"anim.get_float({0})"});
    pure("anim_get_bool", "Get Anim Bool", "Animation", {Pin{"Parameter", T::String, "IsJumping"}}, {Pin{"Value", T::Bool, ""}},
         {"anim.get_bool({0})"});

    // ---- events and timers
    action("call_custom_event", "Call Custom Event", "Utilities|Events", {Pin{"Event", T::String, "MyEvent"}, Pin{"Payload", T::String, ""}},
           "events.emit({1}, {2})", "send signal message fire broadcast", "Runs this object's Custom Event of that name, after the current one");
    action("call_custom_event_on", "Call Custom Event On", "Utilities|Events",
           {Pin{"Target", T::Object, ""}, Pin{"Event", T::String, "MyEvent"}, Pin{"Payload", T::String, ""}}, "events.emit_to({1}, {2}, {3})",
           "send signal message other object", "Runs another object's Custom Event of that name");
    action("set_timer", "Set Timer", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}, Pin{"Seconds", T::Float, "1.0"}, Pin{"Looping", T::Bool, "false"}},
           "timers.set({1}, {2}, {3})", "timer by event interval repeat", "On Timer of that name runs when it runs out (again and again when looping)");
    action("clear_timer", "Clear Timer", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}}, "timers.clear({1})", "stop cancel timer");
    pure("is_timer_active", "Is Timer Active", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}}, {Pin{"Active", T::Bool, ""}},
         {"timers.active({0})"}, "running timer");
    pure("get_timer_remaining", "Get Timer Remaining", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}}, {Pin{"Seconds", T::Float, ""}},
         {"timers.remaining({0})"}, "time left timer");

    // ---- world: spawning, finding, removing
    const auto valued = [&all](std::string key, std::string title, std::string category, std::vector<Pin> data, std::vector<Pin> values,
                               std::vector<std::string> codes, std::string keywords, std::string tip) {
        // An action with values, kept in fields for the nodes after it.
        BlueprintNodeTypeUVE n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Action;
        n.inputs.push_back(Exec());
        for (Pin& p : data) {
            n.inputs.push_back(std::move(p));
        }
        n.outputs.push_back(Exec());
        for (Pin& p : values) {
            n.outputs.push_back(std::move(p));
        }
        n.outputCode = std::move(codes);
        n.tooltip = std::move(tip);
        all.push_back(std::move(n));
    };
    valued("spawn_entity", "Spawn Entity", "World",
           {Pin{"Entity", T::String, "Entities/Cube.uventity"}, Pin{"Location", T::Vector, "0,0,0"}, Pin{"Rotation", T::Vector, "0,0,0"}},
           {Pin{"Return Value", T::Object, ""}}, {"world.spawn({1}, {2}, {3})"}, "spawn actor create instantiate prefab",
           "Makes an entity or prefab file's objects at a place");
    action("destroy_object", "Destroy Object", "World", {Pin{"Object", T::Object, ""}}, "world.destroy({1})", "destroy actor remove delete kill",
           "Removes the object (and what is under it) at the end of the frame");
    action("destroy_self", "Destroy Self", "World", {}, "world.destroy_self()", "destroy actor remove delete kill this");
    action("set_life_span", "Set Life Span", "World", {Pin{"Seconds", T::Float, "5.0"}}, "world.set_life_span({1})", "lifetime expire auto destroy",
           "Removes this object after so many seconds (0 keeps it)");
    pure("get_self", "Self", "World", {}, {Pin{"Self", T::Object, ""}}, {"world.self()"}, "this me reference", "This object");
    pure("find_object_with_tag", "Get Object With Tag", "World", {Pin{"Tag", T::String, "Enemy"}}, {Pin{"Object", T::Object, ""}},
         {"world.find_tag({0})"}, "find first actor tag", "The first object carrying the tag (Gameplay Tags)");
    pure("object_is_valid", "Is Valid Object", "World", {Pin{"Object", T::Object, ""}}, {Pin{"Valid", T::Bool, ""}}, {"world.is_valid({0})"},
         "exists alive none null");

    // ---- another object
    pure("object_get_name", "Get Object Name", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Name", T::String, ""}}, {"object.name({0})"},
         "display name label");
    pure("object_get_location", "Get Object Location", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Location", T::Vector, ""}},
         {"object.get_position({0})"}, "position actor location");
    action("object_set_location", "Set Object Location", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Location", T::Vector, "0,0,0"}},
           "object.set_position({1}, {2})", "position actor teleport move");
    pure("object_get_rotation", "Get Object Rotation", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Rotation", T::Vector, ""}},
         {"object.get_rotation({0})"}, "euler angles");
    action("object_set_rotation", "Set Object Rotation", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Rotation", T::Vector, "0,0,0"}},
           "object.set_rotation({1}, {2})", "euler angles turn");
    pure("object_get_scale", "Get Object Scale", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Scale", T::Vector, ""}}, {"object.get_scale({0})"},
         "size");
    action("object_set_scale", "Set Object Scale", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Scale", T::Vector, "1,1,1"}},
           "object.set_scale({1}, {2})", "size");
    action("object_set_visible", "Set Object Visible", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Visible", T::Bool, "true"}},
           "object.set_visible({1}, {2})", "hide show");
    pure("object_has_tag", "Object Has Tag", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Tag", T::String, "Enemy"}}, {Pin{"Has", T::Bool, ""}},
         {"object.has_tag({0}, {1})"}, "actor has tag");
    action("object_add_tag", "Add Tag To Object", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Tag", T::String, "Enemy"}}, "object.add_tag({1}, {2})");
    action("object_remove_tag", "Remove Tag From Object", "Objects", {Pin{"Object", T::Object, ""}, Pin{"Tag", T::String, "Enemy"}},
           "object.remove_tag({1}, {2})");
    pure("object_distance", "Get Distance Between", "Objects", {Pin{"A", T::Object, ""}, Pin{"B", T::Object, ""}}, {Pin{"Distance", T::Float, ""}},
         {"object.distance({0}, {1})"}, "far near range");
    action("object_attach", "Attach To Object", "Objects", {Pin{"Child", T::Object, ""}, Pin{"Parent", T::Object, ""}}, "object.attach({1}, {2})",
           "parent socket attach actor", "Puts the child under the parent: it moves with it");
    action("object_detach", "Detach From Parent", "Objects", {Pin{"Object", T::Object, ""}}, "object.detach({1})", "unparent detach");
    pure("object_get_parent", "Get Parent", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Parent", T::Object, ""}}, {"object.parent({0})"},
         "owner attach parent");

    // ---- traces
    valued("line_trace", "Line Trace", "Physics|Traces", {Pin{"Start", T::Vector, "0,0,0"}, Pin{"End", T::Vector, "0,0,-10"}},
           {Pin{"Hit", T::Bool, ""}, Pin{"Location", T::Vector, ""}, Pin{"Normal", T::Vector, ""}, Pin{"Hit Object", T::Object, ""},
            Pin{"Distance", T::Float, ""}},
           {"trace.line({1}, {2})", "trace.location()", "trace.normal()", "trace.object()", "trace.distance()"},
           "raycast line trace by channel ray hit", "The first collider between Start and End (never this object)");
    valued("sphere_trace", "Sphere Trace", "Physics|Traces",
           {Pin{"Start", T::Vector, "0,0,0"}, Pin{"End", T::Vector, "0,0,-10"}, Pin{"Radius", T::Float, "0.5"}},
           {Pin{"Hit", T::Bool, ""}, Pin{"Location", T::Vector, ""}, Pin{"Normal", T::Vector, ""}, Pin{"Hit Object", T::Object, ""},
            Pin{"Distance", T::Float, ""}},
           {"trace.sphere({1}, {2}, {3})", "trace.location()", "trace.normal()", "trace.object()", "trace.distance()"},
           "sweep shape cast sphere trace", "A ball moved from Start to End: the first collider it touches");

    // ---- game: time, pause, levels
    pure("get_game_time", "Get Game Time In Seconds", "Game|Time", {}, {Pin{"Seconds", T::Float, ""}}, {"game.time()"}, "time seconds elapsed clock");
    pure("get_delta_seconds", "Get World Delta Seconds", "Game|Time", {}, {Pin{"Seconds", T::Float, ""}}, {"game.delta()"}, "frame time dt");
    pure("get_time_scale", "Get Global Time Dilation", "Game|Time", {}, {Pin{"Scale", T::Float, ""}}, {"game.time_scale()"}, "slow motion speed");
    action("set_time_scale", "Set Global Time Dilation", "Game|Time", {Pin{"Scale", T::Float, "1.0"}}, "game.set_time_scale({1})",
           "slow motion bullet time speed", "Slower (0.1) or faster (up to 32) game time");
    action("pause_game", "Pause Game", "Game", {}, "game.pause()", "pause stop freeze", "Stops the world; a widget or object set to run while paused goes on");
    action("resume_game", "Resume Game", "Game", {}, "game.resume()", "unpause continue");
    pure("is_game_paused", "Is Game Paused", "Game", {}, {Pin{"Paused", T::Bool, ""}}, {"game.is_paused()"}, "paused");
    action("quit_game", "Quit Game", "Game", {}, "game.quit()", "exit close end", "Ends the game (stops Play in the editor)");
    action("open_level", "Open Level", "Game", {Pin{"Level", T::String, "Levels/Main.uvscene"}}, "game.open_level({1})",
           "load map scene travel", "Loads another level once this frame is done");
    pure("get_level_name", "Get Current Level Name", "Game", {}, {Pin{"Level", T::String, ""}}, {"game.level_name()"}, "map scene");

    // ---- saved game data
    action("save_set_float", "Set Game Data (Float)", "Game|Save", {Pin{"Key", T::String, "Score"}, Pin{"Value", T::Float, "0.0"}},
           "save.set_float({1}, {2})", "save game variable store", "A value kept for the whole game, written by Save Game");
    action("save_set_int", "Set Game Data (Int)", "Game|Save", {Pin{"Key", T::String, "Coins"}, Pin{"Value", T::Int, "0"}}, "save.set_int({1}, {2})",
           "save game variable store");
    action("save_set_bool", "Set Game Data (Bool)", "Game|Save", {Pin{"Key", T::String, "DoorOpen"}, Pin{"Value", T::Bool, "true"}},
           "save.set_bool({1}, {2})", "save game variable store");
    action("save_set_string", "Set Game Data (String)", "Game|Save", {Pin{"Key", T::String, "PlayerName"}, Pin{"Value", T::String, ""}},
           "save.set_str({1}, {2})", "save game variable store");
    pure("save_get_float", "Get Game Data (Float)", "Game|Save", {Pin{"Key", T::String, "Score"}, Pin{"Default", T::Float, "0.0"}},
         {Pin{"Value", T::Float, ""}}, {"save.get_float({0}, {1})"}, "load game variable");
    pure("save_get_int", "Get Game Data (Int)", "Game|Save", {Pin{"Key", T::String, "Coins"}, Pin{"Default", T::Int, "0"}}, {Pin{"Value", T::Int, ""}},
         {"save.get_int({0}, {1})"}, "load game variable");
    pure("save_get_bool", "Get Game Data (Bool)", "Game|Save", {Pin{"Key", T::String, "DoorOpen"}, Pin{"Default", T::Bool, "false"}},
         {Pin{"Value", T::Bool, ""}}, {"save.get_bool({0}, {1})"}, "load game variable");
    pure("save_get_string", "Get Game Data (String)", "Game|Save", {Pin{"Key", T::String, "PlayerName"}, Pin{"Default", T::String, ""}},
         {Pin{"Value", T::String, ""}}, {"save.get_str({0}, {1})"}, "load game variable");
    pure("save_has", "Has Game Data", "Game|Save", {Pin{"Key", T::String, "Score"}}, {Pin{"Has", T::Bool, ""}}, {"save.has({0})"}, "exists key");
    action("save_game", "Save Game To Slot", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, "save.write({1})", "save game slot write disk",
           "Writes all the game data to a slot");
    action("load_game", "Load Game From Slot", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, "save.read({1})", "load game slot read disk",
           "Reads the slot's game data back (Get Game Data reads it)");
    pure("does_save_exist", "Does Save Game Exist", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, {Pin{"Exists", T::Bool, ""}},
         {"save.exists({0})"}, "slot");
    action("delete_save", "Delete Save Game In Slot", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, "save.delete({1})", "remove slot");

    // ---- sounds at a place, particles, the view
    action("play_sound_at_location", "Play Sound At Location", "Audio|One Shot",
           {Pin{"Sound", T::String, "Sounds/Hit.wav"}, Pin{"Location", T::Vector, "0,0,0"}, Pin{"Volume", T::Float, "1.0"}},
           "audio.play_at({1}, {2}, {3})", "sfx one shot spawn sound 3d", "A sound file heard from a place, once");
    action("play_sound_2d", "Play Sound 2D", "Audio|One Shot", {Pin{"Sound", T::String, "Sounds/Click.wav"}, Pin{"Volume", T::Float, "1.0"}},
           "audio.play_2d({1}, {2})", "sfx one shot ui music", "A sound file heard everywhere, once (menus, music stings)");
    action("set_particles_active", "Set Particles Active", "Effects", {Pin{"Active", T::Bool, "true"}}, "particles.set_active({1})",
           "emitter vfx activate deactivate", "Starts or stops its Particle Emitter");
    action("burst_particles", "Burst Particles", "Effects", {Pin{"Count", T::Int, "30"}}, "particles.burst({1})", "emitter vfx explosion spawn",
           "Lets out so many particles at once, on top of its rate");
    action("set_active_camera", "Set View Target", "Camera", {Pin{"Camera", T::Object, ""}}, "camera.activate({1})",
           "camera switch view target blend", "Makes that camera the view");
    action("camera_shake", "Camera Shake", "Camera", {Pin{"Strength", T::Float, "0.2"}, Pin{"Seconds", T::Float, "0.4"}}, "camera.shake({1}, {2})",
           "shake impact explosion hit", "Shakes the view, fading out");
    pure("get_camera_forward", "Get Camera Forward", "Camera", {}, {Pin{"Forward", T::Vector, ""}}, {"camera.forward()"}, "view direction aim");
    pure("get_camera_location", "Get Camera Location", "Camera", {}, {Pin{"Location", T::Vector, ""}}, {"camera.location()"}, "view position eye");

    // ---- its look, its clips, its walking
    action("set_material", "Set Material", "Rendering|Material", {Pin{"Material", T::String, "Materials/Red.uvmat"}}, "render.set_material({1})",
           "material swap override change", "Draws every surface with that material (empty: its own again)");
    action("set_transparency", "Set Transparency", "Rendering|Material", {Pin{"Transparency", T::Float, "0.5"}}, "render.set_transparency({1})",
           "fade opacity alpha see through", "0 solid, 1 invisible");
    action("play_animation", "Play Animation", "Animation|Sequencer", {Pin{"Clip", T::String, ""}}, "sequencer.play({1})",
           "play clip anim sequence", "Plays a clip of its Animation Sequencer from the start (by name; empty: the one it has)");
    action("stop_animation", "Stop Animation", "Animation|Sequencer", {}, "sequencer.stop()", "stop clip pause anim");
    action("set_animation_rate", "Set Play Rate", "Animation|Sequencer", {Pin{"Rate", T::Float, "1.0"}}, "sequencer.set_rate({1})",
           "speed play rate reverse");
    pure("is_animation_playing", "Is Animation Playing", "Animation|Sequencer", {}, {Pin{"Playing", T::Bool, ""}}, {"sequencer.is_playing()"});
    action("ai_move_to", "AI Move To", "AI", {Pin{"Destination", T::Vector, "0,0,0"}}, "ai.move_to({1})", "navigate path pathfinding walk go",
           "Its Nav Seeker walks it there over the navigation mesh");
    action("ai_stop", "Stop Movement", "AI", {}, "ai.stop()", "halt stop navigation");
    pure("ai_reached", "Has Reached Destination", "AI", {}, {Pin{"Reached", T::Bool, ""}}, {"ai.reached()"}, "arrived finished");
    pure("ai_status", "Get Path Status", "AI", {}, {Pin{"Status", T::String, ""}}, {"ai.status()"}, "Idle Following Finished Failed");

    // ---- arrays: For Each and the item nodes, one of each per item type
    const std::vector<std::pair<T, std::string>> arrays = {
        {T::ObjectArray, "Object"}, {T::IntArray, "Int"}, {T::FloatArray, "Float"}, {T::StringArray, "String"}};
    for (const auto& [arrayType, word] : arrays) {
        const T item = BlueprintArrayItemTypeUVE(arrayType);
        // "(Float Array)": never the same title as a scalar node ("Add (Float)" is the maths one).
        const std::string suffix = " (" + word + " Array)";
        const std::string key = "_" + word;
        const std::string itemDefault = item == T::Int ? "0" : item == T::Float ? "0.0" : item == T::String ? "" : "";
        flow("for_each" + key, "For Each" + suffix, Role::ForEach, {Exec(), Pin{"Array", arrayType, ""}},
             {Exec("Loop Body"), Pin{"Element", item, ""}, Pin{"Index", T::Int, ""}, Exec("Completed")}, "loop array list foreach every",
             "The Loop Body once for each item of the array");
        pure("array_get" + key, "Get" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Index", T::Int, "0"}},
             {Pin{"Item", item, ""}}, {"{0}[{1}]"}, "array item index element", "The item at that index (the first is 0)");
        action("array_add" + key, "Add" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Item", item, itemDefault}},
               "{1}.push({2})", "array append push insert", "Puts the item at the end (wire the array from its variable)");
        pure("array_contains" + key, "Contains Item" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Item", item, itemDefault}},
             {Pin{"Contains", T::Bool, ""}}, {"{0}.contains({1})"}, "array has find in");
        pure("array_random" + key, "Random Item" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}}, {Pin{"Item", item, ""}},
             {"{0}[math.random_int(0, {0}.length() - 1)]"}, "array pick random any", "One of the items, at random (the array must not be empty)");
        pure("array_length" + key, "Length" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}}, {Pin{"Length", T::Int, ""}},
             {"{0}.length()"}, "array count size num");
        action("array_remove_index" + key, "Remove Index" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}, Pin{"Index", T::Int, "0"}},
               "{1}.remove({2})", "array delete erase", "Takes the item at that index out (wire the array from its variable)");
        action("array_clear" + key, "Clear" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}}, "{1} = array.empty_" + std::string(word == "Object" ? "object" : word == "Int" ? "int" : word == "Float" ? "float" : "string") + "()",
               "array empty reset",
               "Empties the array (wire it from its variable)");
        pure("array_is_empty" + key, "Is Empty" + suffix, "Utilities|Array", {Pin{"Array", arrayType, ""}}, {Pin{"Empty", T::Bool, ""}},
             {"({0}.length() == 0)"}, "array none");
    }
    pure("make_array_object", "Make Array (Object)", "Utilities|Array", {Pin{"0", T::Object, ""}, Pin{"1", T::Object, ""}},
         {Pin{"Array", T::ObjectArray, ""}}, {"[{0}, {1}]"}, "array list new");
    pure("make_array_string", "Make Array (String)", "Utilities|Array", {Pin{"0", T::String, "A"}, Pin{"1", T::String, "B"}, Pin{"2", T::String, "C"}},
         {Pin{"Array", T::StringArray, ""}}, {"[{0}, {1}, {2}]"}, "array list new");
    pure("make_array_float", "Make Array (Float)", "Utilities|Array", {Pin{"0", T::Float, "0.0"}, Pin{"1", T::Float, "1.0"}, Pin{"2", T::Float, "2.0"}},
         {Pin{"Array", T::FloatArray, ""}}, {"[{0}, {1}, {2}]"}, "array list new");
    pure("find_all_with_tag", "Get All Objects With Tag", "World", {Pin{"Tag", T::String, "Enemy"}}, {Pin{"Objects", T::ObjectArray, ""}},
         {"world.find_all_tag({0})"}, "find all actors of tag every", "Every object carrying the tag");
    pure("find_all_by_name", "Get All Objects Named", "World", {Pin{"Name Starts With", T::String, "Enemy"}}, {Pin{"Objects", T::ObjectArray, ""}},
         {"world.find_all_name({0})"}, "find all actors name prefix", "Every object whose name starts so");
    pure("object_get_children", "Get Children", "Objects", {Pin{"Object", T::Object, ""}}, {Pin{"Children", T::ObjectArray, ""}},
         {"object.children({0})"}, "child attached components");

    // ---- text
    pure("int_to_string", "To String (Int)", "Utilities|String", {Pin{"Value", T::Int, "0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"},
         "convert text number");
    pure("float_to_string", "To String (Float)", "Utilities|String", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Text", T::String, ""}}, {"str({0})"},
         "convert text number");
    pure("bool_to_string", "To String (Bool)", "Utilities|String", {Pin{"Value", T::Bool, "false"}}, {Pin{"Text", T::String, ""}}, {"str({0})"},
         "convert text");
    pure("vector_to_string", "To String (Vector)", "Utilities|String", {Pin{"Value", T::Vector, "0,0,0"}}, {Pin{"Text", T::String, ""}},
         {"str({0})"}, "convert text");
    pure("string_to_int", "To Int (String)", "Utilities|String", {Pin{"Text", T::String, "0"}}, {Pin{"Value", T::Int, ""}}, {"text.to_int({0})"},
         "parse convert number");
    pure("string_to_float", "To Float (String)", "Utilities|String", {Pin{"Text", T::String, "0.0"}}, {Pin{"Value", T::Float, ""}},
         {"text.to_float({0})"}, "parse convert number");
    pure("string_contains", "Contains (String)", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"Part", T::String, ""}},
         {Pin{"Contains", T::Bool, ""}}, {"{0}.contains({1})"}, "find substring search");
    pure("string_length", "Length (String)", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Length", T::Int, ""}}, {"{0}.length()"},
         "count characters len");
    pure("string_upper", "To Upper", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Result", T::String, ""}}, {"text.upper({0})"},
         "uppercase capital");
    pure("string_lower", "To Lower", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Result", T::String, ""}}, {"text.lower({0})"},
         "lowercase small");
    pure("string_trim", "Trim", "Utilities|String", {Pin{"Text", T::String, ""}}, {Pin{"Result", T::String, ""}}, {"text.trim({0})"},
         "spaces whitespace strip");
    pure("string_replace", "Replace", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"From", T::String, ""}, Pin{"To", T::String, ""}},
         {Pin{"Result", T::String, ""}}, {"text.replace({0}, {1}, {2})"}, "substitute swap");
    pure("string_split", "Split", "Utilities|String", {Pin{"Text", T::String, "a,b,c"}, Pin{"Separator", T::String, ","}},
         {Pin{"Parts", T::StringArray, ""}}, {"text.split({0}, {1})"}, "parse explode tokens");
    pure("string_substring", "Substring", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"Start", T::Int, "0"}, Pin{"Length", T::Int, "1"}},
         {Pin{"Result", T::String, ""}}, {"text.substring({0}, {1}, {2})"}, "mid slice part");
    pure("string_starts_with", "Starts With", "Utilities|String", {Pin{"Text", T::String, ""}, Pin{"Start", T::String, ""}},
         {Pin{"Result", T::Bool, ""}}, {"text.starts_with({0}, {1})"}, "prefix begins");
    pure("format_text", "Format Text", "Utilities|String", {Pin{"Format", T::String, "Score: {0}"}, Pin{"0", T::String, ""}, Pin{"1", T::String, ""}},
         {Pin{"Result", T::String, ""}}, {"text.format({0}, {1}, {2})"}, "format template placeholder",
         "Puts each value where the format says {0} and {1}");

    // ---- more maths
    pure("float_round", "Round", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Int, ""}}, {"math.round({0})"}, "nearest integer");
    pure("float_ceil", "Ceil", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Int, ""}}, {"math.ceil({0})"}, "round up");
    pure("float_truncate", "Truncate", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Int, ""}}, {"int({0})"}, "integer whole");
    pure("snap_to_grid", "Snap To Grid", "Math|Float", {Pin{"Value", T::Float, "0.0"}, Pin{"Grid", T::Float, "1.0"}}, {Pin{"Result", T::Float, ""}},
         {"math.snap({0}, {1})"}, "round step");
    pure("float_wrap", "Wrap", "Math|Float", {Pin{"Value", T::Float, "0.0"}, Pin{"Min", T::Float, "0.0"}, Pin{"Max", T::Float, "360.0"}},
         {Pin{"Result", T::Float, ""}}, {"math.wrap({0}, {1}, {2})"}, "loop modulo angle");
    pure("float_sign", "Sign", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"math.sign({0})"}, "positive negative");
    pure("vector_rotate_axis", "Rotate Vector Around Axis", "Math|Vector",
         {Pin{"Vector", T::Vector, "1,0,0"}, Pin{"Axis", T::Vector, "0,1,0"}, Pin{"Degrees", T::Float, "90.0"}}, {Pin{"Result", T::Vector, ""}},
         {"math.rotate_about({0}, {1}, {2})"}, "turn spin angle");
    pure("random_unit_vector", "Random Unit Vector", "Math|Random", {}, {Pin{"Vector", T::Vector, ""}}, {"math.random_unit_vector()"},
         "random direction");
    pure("random_point_in_box", "Random Point In Box", "Math|Random", {Pin{"Center", T::Vector, "0,0,0"}, Pin{"Half Size", T::Vector, "5,0,5"}},
         {Pin{"Point", T::Vector, ""}}, {"math.random_in_box({0}, {1})"}, "random location spawn area");

    // ---- objects
    pure("find_object", "Find Object", "Objects", {Pin{"Name", T::String, ""}}, {Pin{"Object", T::Object, ""}}, {"node({0})"}, "get actor by name");
    action("hide_object", "Hide Object", "Objects", {Pin{"Object", T::Object, ""}}, "{1}.hide()");
    action("show_object", "Show Object", "Objects", {Pin{"Object", T::Object, ""}}, "{1}.show()");

    // ---- user interface (Widget Blueprints)
    // Widget events: the named widget's (empty: any widget's). They reach the graph of the widget
    // they belong to, and the graph of every object above it.
    const auto widgetEvent = [&all](std::string key, std::string title, std::string ev, std::string widget, std::vector<Pin> data,
                                    std::string keywords, std::string tip) {
        BlueprintNodeTypeUVE n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = "User Interface|Events";
        n.keywords = std::move(keywords);
        n.role = Role::Event;
        n.event = std::move(ev);
        n.inputs.push_back(Pin{std::move(widget), T::String, ""});
        n.outputs.push_back(Exec());
        for (Pin& p : data) {
            n.outputs.push_back(std::move(p));
        }
        n.tooltip = std::move(tip);
        all.push_back(std::move(n));
    };
    widgetEvent("ui_on_clicked", "On Button Clicked", "ui_clicked", "Button", {Pin{"Button", T::String, ""}}, "press click onclicked widget",
                "A button of the widget was clicked (name it, or leave it empty for any)");
    widgetEvent("ui_on_changed", "On Value Changed", "ui_changed", "Widget", {Pin{"Widget", T::String, ""}}, "slider check box toggle",
                "A slider moved or a check box was ticked");
    widgetEvent("ui_on_committed", "On Text Committed", "ui_committed", "Text Box", {Pin{"Text Box", T::String, ""}, Pin{"Text", T::String, ""}},
                "enter submit input", "A text box was submitted with Enter");
    widgetEvent("ui_on_hovered", "On Hovered", "ui_hovered", "Button", {Pin{"Button", T::String, ""}}, "hover mouse enter over",
                "The pointer came onto a button of the widget");
    widgetEvent("ui_on_unhovered", "On Unhovered", "ui_unhovered", "Button", {Pin{"Button", T::String, ""}}, "hover mouse leave exit",
                "The pointer left a button of the widget");
    {
        BlueprintNodeTypeUVE construct;
        construct.key = "ui_on_construct";
        construct.title = "Event Construct";
        construct.category = "User Interface|Events";
        construct.keywords = "widget created ready begin";
        construct.role = Role::Event;
        construct.event = "ready";
        construct.outputs.push_back(Exec());
        construct.tooltip = "Once, when the widget is made";
        all.push_back(std::move(construct));
    }
    action("ui_set_text", "Set Text", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Text", T::String, "Hello"}},
           "ui.set_text({1}, {2})", "label caption", "A Text or Text Box of the widget, by name");
    pure("ui_get_text", "Get Text", "User Interface", {Pin{"Widget", T::String, "Text"}}, {Pin{"Text", T::String, ""}},
         {"ui.get_text({0})"}, "label caption read");
    action("ui_set_visibility", "Set Visibility", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Visible", T::Bool, "true"}},
           "ui.set_visible({1}, {2})", "hide show collapse");
    action("ui_set_color", "Set Color", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Color", T::Vector, "1,1,1"}},
           "ui.set_color({1}, {2})", "tint colour", "A Text's color, a Button's color, an Image's tint");
    action("ui_set_percent", "Set Percent", "User Interface", {Pin{"Widget", T::String, "ProgressBar"}, Pin{"Percent", T::Float, "0.5"}},
           "ui.set_progress({1}, {2})", "progress bar health fill slider value");
    pure("ui_get_value", "Get Value", "User Interface", {Pin{"Widget", T::String, "Slider"}}, {Pin{"Value", T::Float, ""}},
         {"ui.get_value({0})"}, "slider progress percent");
    pure("ui_is_checked", "Is Checked", "User Interface", {Pin{"Widget", T::String, "CheckBox"}}, {Pin{"Checked", T::Bool, ""}},
         {"ui.is_checked({0})"}, "check box ticked toggle");
    action("ui_set_checked", "Set Is Checked", "User Interface", {Pin{"Widget", T::String, "CheckBox"}, Pin{"Checked", T::Bool, "true"}},
           "ui.set_checked({1}, {2})", "check box tick toggle");
    pure("ui_get_selected_option", "Get Selected Option", "User Interface|Combo Box", {Pin{"Widget", T::String, "ComboBox"}},
         {Pin{"Option", T::String, ""}}, {"ui.get_selected_option({0})"}, "dropdown combo choice");
    pure("ui_get_selected_index", "Get Selected Index", "User Interface|Combo Box", {Pin{"Widget", T::String, "ComboBox"}},
         {Pin{"Index", T::Int, ""}}, {"ui.get_selected_index({0})"}, "dropdown combo choice");
    action("ui_set_selected_index", "Set Selected Index", "User Interface|Combo Box",
           {Pin{"Widget", T::String, "ComboBox"}, Pin{"Index", T::Int, "0"}}, "ui.set_selected_index({1}, {2})", "dropdown combo choice");
    action("ui_add_option", "Add Option", "User Interface|Combo Box", {Pin{"Widget", T::String, "ComboBox"}, Pin{"Option", T::String, "Option"}},
           "ui.add_option({1}, {2})", "dropdown combo item");
    action("ui_show_screen", "Show Screen", "User Interface", {Pin{"Screen", T::String, "Main"}}, "ui.show_screen({1})",
           "page menu switcher designer tab", "Shows one of the widget's screens (its Designer tabs) and hides the others");
    action("ui_show_mouse_cursor", "Set Show Mouse Cursor", "User Interface|Input", {Pin{"Show", T::Bool, "true"}}, "ui.show_cursor({1})",
           "pointer mouse cursor visible input mode ui", "Shows and frees the pointer so the UI can be clicked; false hides it for the game again");
    action("ui_remove", "Remove from Parent", "User Interface", {}, "ui.remove()", "close hide widget",
           "Takes this widget off the screen");
    {
        // Returns the widget it made: an action with a value, kept for the nodes after it.
        BlueprintNodeTypeUVE create;
        create.key = "ui_create_widget";
        create.title = "Create Widget";
        create.category = "User Interface";
        create.keywords = "spawn new widget blueprint hud menu";
        create.role = Role::Action;
        create.inputs = {Exec(), Pin{"Class", T::String, ""}};
        create.outputs = {Exec(), Pin{"Return Value", T::Object, ""}};
        create.outputCode = {"ui.create({1})"};
        create.tooltip = "Makes a Widget Blueprint's widgets (its .uvwbp file); Add to Viewport shows them";
        all.push_back(std::move(create));
    }
    action("ui_set_position", "Set Render Position", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}, Pin{"Position", T::Vector, "0,0,0"}},
           "ui.set_position({1}, {2})", "move translate offset slot", "Its top-left corner, in design pixels (X, Y)");
    action("ui_set_size", "Set Size", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}, Pin{"Size", T::Vector, "120,32,0"}},
           "ui.set_size({1}, {2})", "width height resize", "Its width and height, in design pixels (X, Y)");
    pure("ui_get_position", "Get Render Position", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}}, {Pin{"Position", T::Vector, ""}},
         {"ui.get_position({0})"}, "where location");
    pure("ui_get_size", "Get Size", "User Interface|Layout", {Pin{"Widget", T::String, "Button"}}, {Pin{"Size", T::Vector, ""}},
         {"ui.get_size({0})"}, "width height");
    action("ui_set_opacity", "Set Opacity", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Opacity", T::Float, "1.0"}},
           "ui.set_opacity({1}, {2})", "alpha fade transparency", "A Text's or an Image's opacity (0 to 1)");
    action("ui_set_font_size", "Set Font Size", "User Interface", {Pin{"Widget", T::String, "Text"}, Pin{"Size", T::Float, "24.0"}},
           "ui.set_font_size({1}, {2})", "text size bigger smaller");
    action("ui_play_animation", "Play Widget Animation", "User Interface|Animation", {Pin{"Widget", T::String, "Panel"}},
           "ui.play_tween({1})", "tween animate slide fade", "Plays the widget's Tween from its start");
    action("ui_stop_animation", "Stop Widget Animation", "User Interface|Animation", {Pin{"Widget", T::String, "Panel"}},
           "ui.stop_tween({1})", "tween pause");
    pure("ui_is_hovered", "Is Hovered", "User Interface", {Pin{"Widget", T::String, "Button"}}, {Pin{"Hovered", T::Bool, ""}},
         {"ui.is_hovered({0})"}, "mouse over");
    action("ui_add_to_viewport", "Add to Viewport", "User Interface", {Pin{"Widget", T::Object, ""}, Pin{"Z Order", T::Int, "0"}},
           "ui.add_to_viewport({1}, {2})", "show screen hud", "Shows a widget Create Widget made");

    // ---- gameplay
    pure("attr_has", "Has Attribute", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}}, {Pin{"Has", T::Bool, ""}}, {"attributes.has({0})"});
    pure("attr_get", "Get Attribute", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}}, {Pin{"Value", T::Float, ""}}, {"attributes.get({0})"}, "health stat");
    pure("attr_max", "Get Attribute Max", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}}, {Pin{"Max", T::Float, ""}}, {"attributes.max({0})"});
    action("attr_damage", "Damage", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}, Pin{"Amount", T::Float, "10.0"}},
           "attributes.damage({1}, {2})", "hurt hit");
    action("attr_heal", "Heal", "Gameplay|Attributes", {Pin{"Attribute", T::String, "stamina"}, Pin{"Amount", T::Float, "10.0"}},
           "attributes.heal({1}, {2})", "restore");
    pure("tag_has", "Has Tag", "Gameplay|Tags", {Pin{"Tag", T::String, ""}}, {Pin{"Has", T::Bool, ""}}, {"tags.has({0})"});
    action("tag_add", "Add Tag", "Gameplay|Tags", {Pin{"Tag", T::String, ""}}, "tags.add({1})");
    action("tag_remove", "Remove Tag", "Gameplay|Tags", {Pin{"Tag", T::String, ""}}, "tags.remove({1})");

    // ---- math
    const auto binary = [&pure](const std::string& key, const std::string& title, const std::string& category, T a, T result, const std::string& op,
                                const std::string& keywords) {
        const std::string def = a == T::Int ? "0" : a == T::Vector ? "0,0,0" : a == T::Bool ? "false" : "0.0";
        pure(key, title, category, {Pin{"A", a, def}, Pin{"B", a, def}}, {Pin{"Result", result, ""}}, {"({0} " + op + " {1})"}, keywords);
    };
    binary("float_add", "Add (Float)", "Math|Float", T::Float, T::Float, "+", "plus sum");
    binary("float_sub", "Subtract (Float)", "Math|Float", T::Float, T::Float, "-", "minus");
    binary("float_mul", "Multiply (Float)", "Math|Float", T::Float, T::Float, "*", "times");
    binary("float_div", "Divide (Float)", "Math|Float", T::Float, T::Float, "/", "");
    binary("float_greater", "Greater (Float)", "Math|Compare", T::Float, T::Bool, ">", "more than");
    binary("float_less", "Less (Float)", "Math|Compare", T::Float, T::Bool, "<", "smaller than");
    binary("float_greater_equal", "Greater Or Equal (Float)", "Math|Compare", T::Float, T::Bool, ">=", "");
    binary("float_less_equal", "Less Or Equal (Float)", "Math|Compare", T::Float, T::Bool, "<=", "");
    binary("float_equal", "Equal (Float)", "Math|Compare", T::Float, T::Bool, "==", "same");
    binary("int_add", "Add (Int)", "Math|Int", T::Int, T::Int, "+", "plus sum");
    binary("int_sub", "Subtract (Int)", "Math|Int", T::Int, T::Int, "-", "minus");
    binary("int_mul", "Multiply (Int)", "Math|Int", T::Int, T::Int, "*", "times");
    binary("int_equal", "Equal (Int)", "Math|Compare", T::Int, T::Bool, "==", "same");
    binary("int_greater", "Greater (Int)", "Math|Compare", T::Int, T::Bool, ">", "");
    binary("int_less", "Less (Int)", "Math|Compare", T::Int, T::Bool, "<", "");
    binary("bool_and", "AND", "Math|Boolean", T::Bool, T::Bool, "and", "both");
    binary("bool_or", "OR", "Math|Boolean", T::Bool, T::Bool, "or", "either");
    pure("bool_not", "NOT", "Math|Boolean", {Pin{"A", T::Bool, "false"}}, {Pin{"Result", T::Bool, ""}}, {"(not {0})"}, "invert");
    binary("string_equal", "Equal (String)", "Math|Compare", T::String, T::Bool, "==", "same name");
    pure("float_min", "Min", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"min({0}, {1})"});
    pure("float_max", "Max", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"max({0}, {1})"});
    pure("float_clamp", "Clamp", "Math|Float", {Pin{"Value", T::Float, "0.0"}, Pin{"Min", T::Float, "0.0"}, Pin{"Max", T::Float, "1.0"}},
         {Pin{"Result", T::Float, ""}}, {"clamp({0}, {1}, {2})"}, "limit");
    pure("float_lerp", "Lerp", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "1.0"}, Pin{"Alpha", T::Float, "0.5"}},
         {Pin{"Result", T::Float, ""}}, {"lerp({0}, {1}, {2})"}, "mix interpolate blend");
    pure("float_abs", "Absolute", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"abs({0})"}, "abs");
    pure("float_sqrt", "Square Root", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"sqrt({0})"}, "sqrt");
    pure("float_sin", "Sin", "Math|Trig", {Pin{"Radians", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"sin({0})"}, "sine wave");
    pure("float_cos", "Cos", "Math|Trig", {Pin{"Radians", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"cos({0})"}, "cosine");
    pure("float_floor", "Floor", "Math|Float", {Pin{"Value", T::Float, "0.0"}}, {Pin{"Result", T::Float, ""}}, {"floor({0})"}, "round down");
    pure("make_vector", "Make Vector", "Math|Vector", {Pin{"X", T::Float, "0.0"}, Pin{"Y", T::Float, "0.0"}, Pin{"Z", T::Float, "0.0"}},
         {Pin{"Vector", T::Vector, ""}}, {"vec3({0}, {1}, {2})"}, "vec3 make");
    pure("break_vector", "Break Vector", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}},
         {Pin{"X", T::Float, ""}, Pin{"Y", T::Float, ""}, Pin{"Z", T::Float, ""}}, {"{0}.x", "{0}.y", "{0}.z"}, "split components");
    binary("vector_add", "Add (Vector)", "Math|Vector", T::Vector, T::Vector, "+", "plus");
    binary("vector_sub", "Subtract (Vector)", "Math|Vector", T::Vector, T::Vector, "-", "minus");
    pure("vector_scale", "Scale Vector", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}, Pin{"Scale", T::Float, "1.0"}}, {Pin{"Result", T::Vector, ""}},
         {"({0} * {1})"}, "multiply times");
    pure("vector_length", "Vector Length", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}}, {Pin{"Length", T::Float, ""}}, {"length({0})"}, "size magnitude");
    pure("vector_normalize", "Normalize", "Math|Vector", {Pin{"Vector", T::Vector, "0,0,0"}}, {Pin{"Result", T::Vector, ""}}, {"normalize({0})"}, "direction unit");
    pure("random_float_range", "Random Float In Range", "Math|Random", {Pin{"Min", T::Float, "0.0"}, Pin{"Max", T::Float, "1.0"}},
         {Pin{"Value", T::Float, ""}}, {"math.random_range({0}, {1})"}, "rand");
    pure("random_int_range", "Random Integer In Range", "Math|Random", {Pin{"Min", T::Int, "0"}, Pin{"Max", T::Int, "10"}},
         {Pin{"Value", T::Int, ""}}, {"math.random_int({0}, {1})"}, "rand");
    pure("random_bool", "Random Bool", "Math|Random", {}, {Pin{"Value", T::Bool, ""}}, {"math.random_bool()"}, "rand coin");
    pure("float_pow", "Power", "Math|Float", {Pin{"Base", T::Float, "2.0"}, Pin{"Exponent", T::Float, "2.0"}}, {Pin{"Result", T::Float, ""}},
         {"math.pow({0}, {1})"}, "pow exponent square");
    pure("float_mod", "Modulo (Float)", "Math|Float", {Pin{"A", T::Float, "0.0"}, Pin{"B", T::Float, "1.0"}}, {Pin{"Result", T::Float, ""}},
         {"math.fmod({0}, {1})"}, "remainder fmod");
    pure("float_atan2", "Atan2 (Degrees)", "Math|Trig", {Pin{"Y", T::Float, "0.0"}, Pin{"X", T::Float, "1.0"}}, {Pin{"Degrees", T::Float, ""}},
         {"math.atan2({0}, {1})"}, "angle direction");
    pure("degrees_to_radians", "Degrees To Radians", "Math|Trig", {Pin{"Degrees", T::Float, "0.0"}}, {Pin{"Radians", T::Float, ""}},
         {"({0} * 0.017453292)"}, "convert");
    pure("radians_to_degrees", "Radians To Degrees", "Math|Trig", {Pin{"Radians", T::Float, "0.0"}}, {Pin{"Degrees", T::Float, ""}},
         {"({0} * 57.29578)"}, "convert");
    pure("map_range_clamped", "Map Range Clamped", "Math|Float",
         {Pin{"Value", T::Float, "0.0"}, Pin{"In Min", T::Float, "0.0"}, Pin{"In Max", T::Float, "1.0"}, Pin{"Out Min", T::Float, "0.0"},
          Pin{"Out Max", T::Float, "1.0"}},
         {Pin{"Result", T::Float, ""}}, {"lerp({3}, {4}, clamp(({0} - {1}) / ({2} - {1}), 0.0, 1.0))"}, "remap scale normalize");
    pure("finterp_to", "Interp To (Float)", "Math|Float",
         {Pin{"Current", T::Float, "0.0"}, Pin{"Target", T::Float, "1.0"}, Pin{"Delta Seconds", T::Float, "0.016"}, Pin{"Speed", T::Float, "5.0"}},
         {Pin{"Result", T::Float, ""}}, {"lerp({0}, {1}, clamp({2} * {3}, 0.0, 1.0))"}, "smooth ease approach finterp");
    pure("vinterp_to", "Interp To (Vector)", "Math|Vector",
         {Pin{"Current", T::Vector, "0,0,0"}, Pin{"Target", T::Vector, "0,0,0"}, Pin{"Delta Seconds", T::Float, "0.016"}, Pin{"Speed", T::Float, "5.0"}},
         {Pin{"Result", T::Vector, ""}}, {"({0} + ({1} - {0}) * clamp({2} * {3}, 0.0, 1.0))"}, "smooth ease approach vinterp");
    pure("vector_lerp", "Lerp (Vector)", "Math|Vector", {Pin{"A", T::Vector, "0,0,0"}, Pin{"B", T::Vector, "0,0,0"}, Pin{"Alpha", T::Float, "0.5"}},
         {Pin{"Result", T::Vector, ""}}, {"({0} + ({1} - {0}) * {2})"}, "mix blend");
    pure("vector_distance", "Distance (Vector)", "Math|Vector", {Pin{"A", T::Vector, "0,0,0"}, Pin{"B", T::Vector, "0,0,0"}},
         {Pin{"Distance", T::Float, ""}}, {"length({0} - {1})"}, "between far");
    pure("vector_cross", "Cross Product", "Math|Vector", {Pin{"A", T::Vector, "1,0,0"}, Pin{"B", T::Vector, "0,1,0"}}, {Pin{"Result", T::Vector, ""}},
         {"vec3({0}.y * {1}.z - {0}.z * {1}.y, {0}.z * {1}.x - {0}.x * {1}.z, {0}.x * {1}.y - {0}.y * {1}.x)"}, "cross normal perpendicular");
    const auto select = [&pure](const std::string& key, const std::string& title, T type, const std::string& a, const std::string& b,
                                const std::string& fn) {
        pure(key, title, "Utilities|Select", {Pin{"Pick A", T::Bool, "true"}, Pin{"A", type, a}, Pin{"B", type, b}}, {Pin{"Result", type, ""}},
             {"math." + fn + "({0}, {1}, {2})"}, "select choose ternary if", "A when Pick A is true, else B");
    };
    select("select_float", "Select Float", T::Float, "0.0", "1.0", "select_float");
    select("select_int", "Select Int", T::Int, "0", "1", "select_int");
    select("select_vector", "Select Vector", T::Vector, "0,0,0", "1,1,1", "select_vector");
    select("select_string", "Select String", T::String, "A", "B", "select_string");
    pure("vector_dot", "Dot Product", "Math|Vector", {Pin{"A", T::Vector, "0,0,0"}, Pin{"B", T::Vector, "0,0,0"}}, {Pin{"Result", T::Float, ""}},
         {"({0}.x * {1}.x + {0}.y * {1}.y + {0}.z * {1}.z)"}, "dot");

    // ---- variables (their pins come from the variable)
    BlueprintNodeTypeUVE get;
    get.key = "get_variable";
    get.title = "Get";
    get.category = "Variables";
    get.role = Role::GetVariable;
    get.outputs = {Pin{"Value", T::Float, ""}};
    all.push_back(get);
    BlueprintNodeTypeUVE set;
    set.key = "set_variable";
    set.title = "Set";
    set.category = "Variables";
    set.role = Role::SetVariable;
    set.inputs = {Exec(), Pin{"Value", T::Float, ""}};
    set.outputs = {Exec(), Pin{"Value", T::Float, ""}};
    all.push_back(set);
    // ---- networking (delegates exclusively to Engine/Runtime/Networking/ host bindings)
    {
        const auto toPinType = [](const Blueprint::Nodes::Networking::NetworkingPinTypeUVE t) {
            using NT = Blueprint::Nodes::Networking::NetworkingPinTypeUVE;
            switch (t) {
                case NT::Bool: return T::Bool;
                case NT::Int: return T::Int;
                case NT::Float: return T::Float;
                case NT::String: return T::String;
                case NT::Vector: return T::Vector;
            }
            return T::Int;
        };
        for (const auto& spec : Blueprint::Nodes::Networking::GetNetworkingBlueprintNodesUVE()) {
            std::vector<Pin> inPins;
            for (const auto& p : spec.inputs) {
                inPins.push_back(Pin{std::string(p.name), toPinType(p.type), std::string(p.defaultText)});
            }
            if (spec.kind == Blueprint::Nodes::Networking::NetworkingNodeKindUVE::Action) {
                action(std::string(spec.key), std::string(spec.title), std::string(spec.category),
                       std::move(inPins), std::string(spec.expression), std::string(spec.keywords),
                       std::string(spec.tooltip));
            } else {
                std::vector<Pin> outPins;
                for (const auto& p : spec.outputs) {
                    outPins.push_back(Pin{std::string(p.name), toPinType(p.type), std::string(p.defaultText)});
                }
                pure(std::string(spec.key), std::string(spec.title), std::string(spec.category),
                     std::move(inPins), std::move(outPins), {std::string(spec.expression)},
                     std::string(spec.keywords), std::string(spec.tooltip));
            }
        }
    }
    AssignScopesUVE(all);
    return all;
}

[[nodiscard]] std::string IdentifierUVE(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ? c : '_');
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

/// An unwired input's typed value as UVScript.
[[nodiscard]] std::string LiteralUVE(const T type, const std::string& text) {
    switch (type) {
        case T::Bool: return text == "true" || text == "1" ? "true" : "false";
        case T::Int: {
            try {
                return std::to_string(std::stoll(text.empty() ? "0" : text));
            } catch (...) {
                return "0";
            }
        }
        case T::Float: {
            double value = 0.0;
            try {
                value = text.empty() ? 0.0 : std::stod(text);
            } catch (...) {
            }
            std::ostringstream s;
            s.precision(9);
            s << value;
            std::string out = s.str();
            if (out.find_first_of(".eE") == std::string::npos) {
                out += ".0";
            }
            return out;
        }
        case T::Vector: {
            std::string clean;
            for (const char c : text) {
                if (c != '(' && c != ')' && c != ' ') {
                    clean.push_back(c);
                }
            }
            std::vector<std::string> parts;
            std::stringstream in(clean);
            for (std::string part; std::getline(in, part, ',');) {
                parts.push_back(part);
            }
            parts.resize(3U);
            return "vec3(" + LiteralUVE(T::Float, parts[0]) + ", " + LiteralUVE(T::Float, parts[1]) + ", " + LiteralUVE(T::Float, parts[2]) + ")";
        }
        case T::String: {
            std::string out = "\"";
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out.push_back('\\');
                }
                if (c == '{' || c == '}') {
                    out.push_back(c);   // doubled: a typed value is literal text, never interpolation
                }
                out.push_back(c == '\n' ? ' ' : c);
            }
            return out + "\"";
        }
        // An unwired object pin means this object ("self"), as the palette's tooltips say.
        case T::Object: return "world.self()";
        case T::ObjectArray:
        case T::IntArray:
        case T::FloatArray:
        case T::StringArray: return "[]";
        case T::Exec: break;
    }
    return "none";
}

/// Fills `{n}` in `pattern` with `args[n]`.
[[nodiscard]] std::string FormatUVE(const std::string& pattern, const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0U; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const std::size_t close = pattern.find('}', i);
            if (close != std::string::npos) {
                const std::string index = pattern.substr(i + 1U, close - i - 1U);
                if (!index.empty() && std::all_of(index.begin(), index.end(), [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
                    const std::size_t n = static_cast<std::size_t>(std::stoul(index));
                    out += n < args.size() ? args[n] : std::string{"none"};
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

/// The timer a Retriggerable Delay node waits on.
[[nodiscard]] std::string RetriggerTimerUVE(const std::uint32_t id) { return "__delay" + std::to_string(id); }

class TranslatorUVE final {
public:
    explicit TranslatorUVE(const BlueprintAssetUVE& blueprint) : m_bp(blueprint) {
        for (const BlueprintNodeUVE& node : m_bp.nodes) {
            m_nodes[node.id] = &node;
        }
    }

    BlueprintTranslationUVE Run(const std::string_view objectName, const std::string_view objectKind) {
        Line("entity " + IdentifierUVE(objectName.empty() ? std::string_view{"Object"} : objectName) + " : " + std::string(objectKind), 0U);
        Line("", 0U);
        for (const BlueprintVariableUVE& variable : m_bp.variables) {
            const std::string keyword = variable.exposed ? "export" : "var";
            const std::string init = variable.type == T::Object ? std::string{} : " = " + LiteralUVE(variable.type, variable.defaultText);
            Line(keyword + " " + IdentifierUVE(variable.name) + ": " + std::string(BlueprintPinTypeScriptNameUVE(variable.type)) + init, 0U);
        }
        // Event chains, grouped by event so two Event Tick nodes run one after the other.
        std::map<std::string, std::vector<const BlueprintNodeUVE*>> byEvent;
        std::vector<std::string> order;
        for (const BlueprintNodeUVE& node : m_bp.nodes) {
            const BlueprintNodeTypeUVE* const type = FindBlueprintNodeTypeUVE(node.type);
            if (type == nullptr) {
                m_out.problems.emplace_back(node.id, "unknown node \"" + node.type + "\"");
                continue;
            }
            if (type->role == Role::Event) {
                if (!byEvent.contains(type->event)) {
                    order.push_back(type->event);
                }
                byEvent[type->event].push_back(&node);
            }
            if (type->role == Role::RetriggerableDelay) {
                m_delays.push_back(node.id);
            }
        }
        // A Retriggerable Delay's Completed runs when its timer does.
        if (!m_delays.empty() && !byEvent.contains("timer")) {
            order.push_back("timer");
            byEvent["timer"];
        }
        // Body first (it declares the Do Once / Flip Flop fields), then fields, then the body.
        std::vector<std::pair<std::string, std::uint32_t>> body;
        std::swap(body, m_lines);
        for (const std::string& event : order) {
            const auto& nodes = byEvent[event];
            const BlueprintNodeTypeUVE* const type =
                nodes.empty() ? FindBlueprintNodeTypeUVE("event_timer") : FindBlueprintNodeTypeUVE(nodes.front()->type);
            const std::uint32_t at = nodes.empty() ? m_delays.front() : nodes.front()->id;
            std::string params;
            for (std::size_t i = 1U; i < type->outputs.size(); ++i) {
                params += (params.empty() ? "" : ", ") + std::string("arg") + std::to_string(i);
            }
            Line("", 0U);
            Line("on " + event + (params.empty() ? std::string{} : "(" + params + ")") + ":", at);
            const std::size_t before = m_lines.size();
            for (const BlueprintNodeUVE* node : nodes) {
                // A widget event names its widget: only that one's (empty: every widget's).
                const std::string widget = !type->inputs.empty() && !node->inputValues.empty() ? node->inputValues[0] : std::string{};
                if (!widget.empty() && type->outputs.size() > 1U) {
                    Line("    if arg1 == " + LiteralUVE(T::String, widget) + ":", node->id);
                    EmitBlock(node->id, 0U, 2);
                } else {
                    EmitFrom(node->id, 0U, 1);
                }
            }
            if (event == "timer") {
                for (const std::uint32_t delay : m_delays) {
                    Line("    if arg1 == \"" + RetriggerTimerUVE(delay) + "\":", delay);
                    EmitBlock(delay, 0U, 2);
                }
            }
            if (m_lines.size() == before) {
                Line("    pass", at);
            }
        }
        std::vector<std::pair<std::string, std::uint32_t>> handlers;
        std::swap(handlers, m_lines);
        m_lines = std::move(body);
        for (const std::string& field : m_fields) {
            Line(field, 0U);
        }
        for (auto& line : handlers) {
            m_lines.push_back(std::move(line));
        }
        for (const auto& [text, node] : m_lines) {
            m_out.source += text + "\n";
            m_out.lineNodes.push_back(node);
        }
        return std::move(m_out);
    }

private:
    void Line(std::string text, const std::uint32_t node) { m_lines.emplace_back(std::move(text), node); }
    /// The field an action's value output `pin` is kept in (`ret7`, then `ret7_2` ...).
    static std::string ReturnFieldUVE(const std::string& tag, const std::uint32_t pin) {
        return pin <= 1U ? "ret" + tag : "ret" + tag + "_" + std::to_string(pin);
    }
    /// A field once per node, however many exec wires lead into it.
    void FieldOnceUVE(const std::string& name, const std::string& declaration) {
        if (m_declared.insert(name).second) {
            m_fields.push_back(declaration);
        }
    }
    /// An unwired input's typed value (a field's starting value cannot be an expression).
    [[nodiscard]] std::string TypedValueUVE(const BlueprintNodeUVE& node, const std::uint32_t pin) const {
        const BlueprintNodeTypeUVE* const type = FindBlueprintNodeTypeUVE(node.type);
        if (type == nullptr || pin >= type->inputs.size()) {
            return "none";
        }
        const std::string& typed = pin < node.inputValues.size() && !node.inputValues[pin].empty() ? node.inputValues[pin] : type->inputs[pin].defaultText;
        return LiteralUVE(type->inputs[pin].type, typed);
    }
    static std::string Indent(const int depth) { return std::string(static_cast<std::size_t>(depth) * 4U, ' '); }

    [[nodiscard]] const BlueprintWireUVE* WireFromOutput(const std::uint32_t node, const std::uint32_t pin) const {
        for (const BlueprintWireUVE& wire : m_bp.wires) {
            if (wire.fromNode == node && wire.fromPin == pin) {
                return &wire;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const BlueprintWireUVE* WireToInput(const std::uint32_t node, const std::uint32_t pin) const {
        for (const BlueprintWireUVE& wire : m_bp.wires) {
            if (wire.toNode == node && wire.toPin == pin) {
                return &wire;
            }
        }
        return nullptr;
    }

    /// Follows the exec wire out of `node`'s output `pin` and writes what it leads to.
    void EmitFrom(const std::uint32_t node, const std::uint32_t pin, const int depth) {
        if (const BlueprintWireUVE* wire = WireFromOutput(node, pin)) {
            EmitNode(wire->toNode, depth, wire->toPin);
        }
    }

    /// Writes a block; an empty one gets `pass`.
    void EmitBlock(const std::uint32_t node, const std::uint32_t pin, const int depth) {
        const std::size_t before = m_lines.size();
        EmitFrom(node, pin, depth);
        if (m_lines.size() == before) {
            Line(Indent(depth) + "pass", node);
        }
    }

    void EmitNode(const std::uint32_t id, const int depth, const std::uint32_t entry = 0U) {
        const auto found = m_nodes.find(id);
        if (found == m_nodes.end()) {
            return;
        }
        if (m_active.contains(id) || depth > 64) {
            m_out.problems.emplace_back(id, "its exec wires lead back into themselves");
            return;
        }
        const BlueprintNodeUVE& node = *found->second;
        const BlueprintNodeTypeUVE* const type = FindBlueprintNodeTypeUVE(node.type);
        if (type == nullptr) {
            return;
        }
        m_active.insert(id);
        const std::string pad = Indent(depth);
        const std::string tag = std::to_string(id);
        switch (type->role) {
            case Role::Action: {
                std::vector<std::string> args;
                for (std::uint32_t i = 0U; i < type->inputs.size(); ++i) {
                    args.push_back(type->inputs[i].type == T::Exec ? std::string{} : InputExpr(node, i));
                }
                if (!type->outputCode.empty() && type->outputs.size() > 1U) {
                    // An action with values: each kept in a field for the nodes after it, the
                    // first from the call itself and the rest read right after it.
                    for (std::uint32_t out = 1U; out < type->outputs.size() && out - 1U < type->outputCode.size(); ++out) {
                        const T valueType = type->outputs[out].type;
                        const std::string field = ReturnFieldUVE(tag, out);
                        if (m_declared.insert(field).second) {
                            m_fields.push_back("var " + field + ": " + std::string(BlueprintPinTypeScriptNameUVE(valueType)) +
                                               (valueType == T::Object ? std::string{} : " = " + LiteralUVE(valueType, "")));
                        }
                        Line(pad + field + " = " + FormatUVE(type->outputCode[out - 1U], args), id);
                    }
                } else {
                    Line(pad + FormatUVE(type->code, args), id);
                }
                EmitFrom(id, 0U, depth);
                break;
            }
            case Role::SetVariable: {
                Line(pad + IdentifierUVE(node.variable) + " = " + InputExpr(node, 1U), id);
                EmitFrom(id, 0U, depth);
                break;
            }
            case Role::Branch: {
                Line(pad + "if " + InputExpr(node, 1U) + ":", id);
                EmitBlock(id, 0U, depth + 1);
                if (WireFromOutput(id, 1U) != nullptr) {
                    Line(pad + "else:", id);
                    EmitBlock(id, 1U, depth + 1);
                }
                break;
            }
            case Role::Sequence:
                for (std::uint32_t pin = 0U; pin < type->outputs.size(); ++pin) {
                    EmitFrom(id, pin, depth);
                }
                break;
            case Role::ForLoop:
                Line(pad + "for i" + tag + " in " + InputExpr(node, 1U) + "..(" + InputExpr(node, 2U) + " + 1):", id);
                EmitBlock(id, 0U, depth + 1);
                EmitFrom(id, 2U, depth);
                break;
            case Role::While:
                Line(pad + "while " + InputExpr(node, 1U) + ":", id);
                EmitBlock(id, 0U, depth + 1);
                EmitFrom(id, 1U, depth);
                break;
            case Role::DoOnce:
                FieldOnceUVE("once" + tag, "var once" + tag + " = false");
                Line(pad + "if not once" + tag + ":", id);
                Line(pad + "    once" + tag + " = true", id);
                EmitFrom(id, 0U, depth + 1);
                break;
            case Role::FlipFlop:
                FieldOnceUVE("flip" + tag, "var flip" + tag + " = false");
                Line(pad + "flip" + tag + " = not flip" + tag, id);
                Line(pad + "if flip" + tag + ":", id);
                EmitBlock(id, 0U, depth + 1);
                if (WireFromOutput(id, 1U) != nullptr) {
                    Line(pad + "else:", id);
                    EmitBlock(id, 1U, depth + 1);
                }
                break;
            case Role::Delay:
                Line(pad + "wait " + InputExpr(node, 1U), id);
                EmitFrom(id, 0U, depth);
                break;
            case Role::IsValid:
                Line(pad + "if " + InputExpr(node, 1U) + " != none:", id);
                EmitBlock(id, 0U, depth + 1);
                if (WireFromOutput(id, 1U) != nullptr) {
                    Line(pad + "else:", id);
                    EmitBlock(id, 1U, depth + 1);
                }
                break;
            case Role::Gate: {
                // Open unless "Start Closed": Enter passes only while open.
                FieldOnceUVE("gate" + tag, std::string("var gate") + tag + " = " + (TypedValueUVE(node, 4U) == "true" ? "false" : "true"));
                if (entry == 1U) {
                    Line(pad + "gate" + tag + " = true", id);
                } else if (entry == 2U) {
                    Line(pad + "gate" + tag + " = false", id);
                } else if (entry == 3U) {
                    Line(pad + "gate" + tag + " = not gate" + tag, id);
                } else {
                    Line(pad + "if gate" + tag + ":", id);
                    EmitBlock(id, 0U, depth + 1);
                }
                break;
            }
            case Role::DoN: {
                FieldOnceUVE("count" + tag, "var count" + tag + ": int = 0");
                if (entry == 2U) {
                    Line(pad + "count" + tag + " = 0", id);
                } else {
                    Line(pad + "if count" + tag + " < " + InputExpr(node, 1U) + ":", id);
                    Line(pad + "    count" + tag + " += 1", id);
                    EmitFrom(id, 0U, depth + 1);
                }
                break;
            }
            case Role::MultiGate: {
                FieldOnceUVE("step" + tag, "var step" + tag + ": int = 0");
                const std::uint32_t outs = static_cast<std::uint32_t>(type->outputs.size());
                if (entry == 1U) {
                    Line(pad + "step" + tag + " = 0", id);
                    break;
                }
                // Taken before it runs on, so a Delay further down cannot run an output twice.
                Line(pad + "if " + InputExpr(node, 2U) + " and step" + tag + " >= " + std::to_string(outs) + ":", id);
                Line(pad + "    step" + tag + " = 0", id);
                Line(pad + "let at" + tag + " = step" + tag, id);
                Line(pad + "step" + tag + " += 1", id);
                bool first = true;
                for (std::uint32_t out = 0U; out < outs; ++out) {
                    if (WireFromOutput(id, out) == nullptr) {
                        continue;
                    }
                    Line(pad + (first ? "if " : "elif ") + "at" + tag + " == " + std::to_string(out) + ":", id);
                    EmitBlock(id, out, depth + 1);
                    first = false;
                }
                break;
            }
            case Role::RetriggerableDelay:
                // A timer of its own: entering again starts it over; what follows runs in `on timer`.
                Line(pad + "timers.set(\"" + RetriggerTimerUVE(id) + "\", " + InputExpr(node, 1U) + ", false)", id);
                break;
            case Role::SwitchInt:
            case Role::SwitchString: {
                const bool text = type->role == Role::SwitchString;
                const std::uint32_t cases = static_cast<std::uint32_t>(type->outputs.size()) - 1U;
                Line(pad + "let pick" + tag + " = " + InputExpr(node, 1U), id);
                bool first = true;
                for (std::uint32_t out = 0U; out < cases; ++out) {
                    if (WireFromOutput(id, out) == nullptr) {
                        continue;
                    }
                    const std::string value = text ? InputExpr(node, 2U + out) : std::to_string(out);
                    Line(pad + (first ? "if " : "elif ") + "pick" + tag + " == " + value + ":", id);
                    EmitBlock(id, out, depth + 1);
                    first = false;
                }
                if (WireFromOutput(id, cases) != nullptr) {
                    if (first) {
                        EmitFrom(id, cases, depth);
                    } else {
                        Line(pad + "else:", id);
                        EmitBlock(id, cases, depth + 1);
                    }
                }
                break;
            }
            case Role::ForEach: {
                // Over a copy taken once, so the body may change the array it walks.
                const std::string items = "items" + tag;
                FieldOnceUVE(items, "var " + items + ": " + std::string(BlueprintPinTypeScriptNameUVE(type->inputs[1].type)) + " = []");
                Line(pad + items + " = " + InputExpr(node, 1U), id);
                Line(pad + "for i" + tag + " in 0.." + items + ".length():", id);
                Line(pad + "    let e" + tag + " = " + items + "[i" + tag + "]", id);
                EmitFrom(id, 0U, depth + 1);
                EmitFrom(id, 3U, depth);
                break;
            }
            case Role::Event:
                m_out.problems.emplace_back(id, "an event cannot be wired into");
                break;
            case Role::Pure:
            case Role::GetVariable:
                m_out.problems.emplace_back(id, "has no exec input");
                break;
        }
        m_active.erase(id);
    }

    /// The expression for input `pin` of `node`: the wired output's, else the typed value.
    [[nodiscard]] std::string InputExpr(const BlueprintNodeUVE& node, const std::uint32_t pin) {
        const BlueprintNodePinsUVE pins = ResolveBlueprintNodePinsUVE(m_bp, node);
        if (const BlueprintWireUVE* wire = WireToInput(node.id, pin)) {
            return OutputExpr(wire->fromNode, wire->fromPin);
        }
        if (pin >= pins.inputs.size()) {
            return "none";
        }
        const std::string& typed = pin < node.inputValues.size() && !node.inputValues[pin].empty() ? node.inputValues[pin] : pins.inputs[pin].defaultText;
        return LiteralUVE(pins.inputs[pin].type, typed);
    }

    [[nodiscard]] std::string OutputExpr(const std::uint32_t id, const std::uint32_t pin) {
        const auto found = m_nodes.find(id);
        if (found == m_nodes.end()) {
            return "none";
        }
        const BlueprintNodeUVE& node = *found->second;
        const BlueprintNodeTypeUVE* const type = FindBlueprintNodeTypeUVE(node.type);
        if (type == nullptr) {
            return "none";
        }
        const std::string tag = std::to_string(id);
        switch (type->role) {
            case Role::Event: return "arg" + std::to_string(pin);
            case Role::Action: return type->outputCode.empty() ? std::string{"none"} : ReturnFieldUVE(tag, pin);
            case Role::DoN: return "count" + tag;
            case Role::ForEach: return pin == 1U ? "e" + tag : "i" + tag;
            case Role::GetVariable:
            case Role::SetVariable: return IdentifierUVE(node.variable);
            case Role::ForLoop: return "i" + tag;
            case Role::FlipFlop: return "flip" + tag;
            case Role::Pure: {
                if (m_pureDepth > 64 || pin >= type->outputCode.size()) {
                    m_out.problems.emplace_back(id, "its data wires lead back into themselves");
                    return "none";
                }
                ++m_pureDepth;
                std::vector<std::string> args;
                for (std::uint32_t i = 0U; i < type->inputs.size(); ++i) {
                    args.push_back(InputExpr(node, i));
                }
                --m_pureDepth;
                return "(" + FormatUVE(type->outputCode[pin], args) + ")";
            }
            default: return "none";
        }
    }

    const BlueprintAssetUVE& m_bp;
    std::map<std::uint32_t, const BlueprintNodeUVE*> m_nodes;
    std::vector<std::pair<std::string, std::uint32_t>> m_lines;
    std::vector<std::string> m_fields;
    std::set<std::uint32_t> m_active;
    std::set<std::string> m_declared;
    std::vector<std::uint32_t> m_delays;
    int m_pureDepth = 0;
    BlueprintTranslationUVE m_out;
};

} // namespace

const std::vector<BlueprintNodeTypeUVE>& GetBlueprintNodeTypesUVE() {
    static const std::vector<BlueprintNodeTypeUVE> all = BuildCatalogueUVE();
    return all;
}

const BlueprintNodeTypeUVE* FindBlueprintNodeTypeUVE(const std::string_view key) {
    for (const BlueprintNodeTypeUVE& type : GetBlueprintNodeTypesUVE()) {
        if (type.key == key) {
            return &type;
        }
    }
    return nullptr;
}

std::string_view BlueprintPinTypeScriptNameUVE(const BlueprintPinTypeUVE type) noexcept {
    switch (type) {
        case T::Bool: return "bool";
        case T::Int: return "int";
        case T::Float: return "float";
        case T::Vector: return "vec3";
        case T::String: return "str";
        case T::Object: return "Object3D";
        case T::ObjectArray: return "list[Object3D]";
        case T::IntArray: return "list[int]";
        case T::FloatArray: return "list[float]";
        case T::StringArray: return "list[str]";
        case T::Exec: break;
    }
    return "none";
}

BlueprintNodePinsUVE ResolveBlueprintNodePinsUVE(const BlueprintAssetUVE& blueprint, const BlueprintNodeUVE& node) {
    BlueprintNodePinsUVE pins;
    const BlueprintNodeTypeUVE* const type = FindBlueprintNodeTypeUVE(node.type);
    if (type == nullptr) {
        return pins;
    }
    pins.inputs = type->inputs;
    pins.outputs = type->outputs;
    if (type->role == Role::GetVariable || type->role == Role::SetVariable) {
        const auto variable = std::find_if(blueprint.variables.begin(), blueprint.variables.end(),
                                           [&node](const BlueprintVariableUVE& v) { return v.name == node.variable; });
        const T value = variable != blueprint.variables.end() ? variable->type : T::Float;
        const std::string name = variable != blueprint.variables.end() ? variable->name : node.variable;
        for (Pin& pin : pins.inputs) {
            if (pin.type != T::Exec) {
                pin.type = value;
                pin.name = name;
                pin.defaultText = variable != blueprint.variables.end() ? variable->defaultText : std::string{};
            }
        }
        for (Pin& pin : pins.outputs) {
            if (pin.type != T::Exec) {
                pin.type = value;
                pin.name = name;
            }
        }
    }
    return pins;
}

BlueprintTranslationUVE TranslateBlueprintToUVScriptUVE(const BlueprintAssetUVE& blueprint, const std::string_view objectName,
                                                       const std::string_view objectKind) {
    TranslatorUVE translator(blueprint);
    return translator.Run(objectName, objectKind);
}

} // namespace UVE::Asset
