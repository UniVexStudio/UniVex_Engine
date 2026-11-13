// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_nodes.h"

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

    // ---- flow control
    flow("branch", "Branch", Role::Branch, {Exec(), Pin{"Condition", T::Bool, "false"}}, {Exec("True"), Exec("False")}, "if else", "One way or the other");
    flow("sequence", "Sequence", Role::Sequence, {Exec()}, {Exec("Then 0"), Exec("Then 1"), Exec("Then 2"), Exec("Then 3")}, "then order", "Each output in turn");
    flow("for_loop", "For Loop", Role::ForLoop, {Exec(), Pin{"First Index", T::Int, "0"}, Pin{"Last Index", T::Int, "9"}},
         {Exec("Loop Body"), Pin{"Index", T::Int, ""}, Exec("Completed")}, "repeat count", "From First to Last, inclusive");
    flow("while_loop", "While Loop", Role::While, {Exec(), Pin{"Condition", T::Bool, "false"}}, {Exec("Loop Body"), Exec("Completed")}, "repeat", "While the condition holds");
    flow("do_once", "Do Once", Role::DoOnce, {Exec()}, {Exec("Completed")}, "single first", "Only the first time");
    flow("flip_flop", "Flip Flop", Role::FlipFlop, {Exec()}, {Exec("A"), Exec("B"), Pin{"Is A", T::Bool, ""}}, "toggle alternate", "A, then B, then A ...");
    flow("delay", "Delay", Role::Delay, {Exec(), Pin{"Duration", T::Float, "0.5"}}, {Exec("Completed")}, "wait timer seconds", "Waits, then goes on");
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
    pure("input_pressed", "Is Action Pressed", "Input", {Pin{"Action", T::String, "jump"}}, {Pin{"Pressed", T::Bool, ""}}, {"input.pressed({0})"}, "key button just");
    pure("input_held", "Is Action Held", "Input", {Pin{"Action", T::String, "jump"}}, {Pin{"Held", T::Bool, ""}}, {"input.held({0})"}, "key button down");
    pure("input_released", "Was Action Released", "Input", {Pin{"Action", T::String, "jump"}}, {Pin{"Released", T::Bool, ""}}, {"input.released({0})"}, "key button up");
    pure("input_axis", "Get Axis", "Input", {Pin{"Negative", T::String, "left"}, Pin{"Positive", T::String, "right"}}, {Pin{"Value", T::Float, ""}},
         {"input.axis({0}, {1})"}, "move stick");

    // ---- audio
    action("audio_play", "Play Sound", "Audio", {}, "audio.play()", "sound sfx", "Its AudioSource");
    action("audio_stop", "Stop Sound", "Audio", {}, "audio.stop()", "sound sfx", "Its AudioSource");
    pure("audio_is_playing", "Is Sound Playing", "Audio", {}, {Pin{"Playing", T::Bool, ""}}, {"audio.is_playing()"});
    action("set_volume", "Set Volume", "Audio", {Pin{"Volume", T::Float, "1.0"}}, "volume = {1}");
    action("set_pitch", "Set Pitch", "Audio", {Pin{"Pitch", T::Float, "1.0"}}, "pitch = {1}");

    // ---- animation
    action("montage_play", "Play Montage", "Animation", {Pin{"Montage", T::String, "Attack"}}, "montage.play({1})", "attack anim", "On its MontagePlayer, by the montage's name");
    action("montage_play_section", "Play Montage Section", "Animation", {Pin{"Montage", T::String, "Attack"}, Pin{"Section", T::String, "Default"}},
           "montage.play_section({1}, {2})", "combo");
    action("montage_jump_to", "Jump To Section", "Animation", {Pin{"Section", T::String, "Default"}}, "montage.jump_to({1})", "combo next");
    action("montage_stop", "Stop Montage", "Animation", {}, "montage.stop()");
    pure("montage_is_playing", "Is Montage Playing", "Animation", {}, {Pin{"Playing", T::Bool, ""}}, {"montage.is_playing()"});
    pure("montage_section", "Current Section", "Animation", {}, {Pin{"Section", T::String, ""}}, {"montage.section()"});
    pure("montage_curve", "Montage Curve", "Animation", {Pin{"Curve", T::String, ""}}, {Pin{"Value", T::Float, ""}}, {"montage.curve({0})"});

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
                    continue;   // interpolation braces: a typed value is literal text
                }
                out.push_back(c == '\n' ? ' ' : c);
            }
            return out + "\"";
        }
        case T::Object: return "none";
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
        }
        // Body first (it declares the Do Once / Flip Flop fields), then fields, then the body.
        std::vector<std::pair<std::string, std::uint32_t>> body;
        std::swap(body, m_lines);
        for (const std::string& event : order) {
            const auto& nodes = byEvent[event];
            const BlueprintNodeTypeUVE* const type = FindBlueprintNodeTypeUVE(nodes.front()->type);
            std::string params;
            for (std::size_t i = 1U; i < type->outputs.size(); ++i) {
                params += (params.empty() ? "" : ", ") + std::string("arg") + std::to_string(i);
            }
            Line("", 0U);
            Line("on " + event + (params.empty() ? std::string{} : "(" + params + ")") + ":", nodes.front()->id);
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
            if (m_lines.size() == before) {
                Line("    pass", nodes.front()->id);
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
            EmitNode(wire->toNode, depth);
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

    void EmitNode(const std::uint32_t id, const int depth) {
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
                    // An action with a value: kept in a field for the nodes after it.
                    const T valueType = type->outputs[1].type;
                    m_fields.push_back("var ret" + tag + ": " + std::string(BlueprintPinTypeScriptNameUVE(valueType)) +
                                       (valueType == T::Object ? std::string{} : " = " + LiteralUVE(valueType, "")));
                    Line(pad + "ret" + tag + " = " + FormatUVE(type->outputCode[0], args), id);
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
                m_fields.push_back("var once" + tag + " = false");
                Line(pad + "if not once" + tag + ":", id);
                Line(pad + "    once" + tag + " = true", id);
                EmitFrom(id, 0U, depth + 1);
                break;
            case Role::FlipFlop:
                m_fields.push_back("var flip" + tag + " = false");
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
            case Role::Action: return type->outputCode.empty() ? std::string{"none"} : "ret" + tag;
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
