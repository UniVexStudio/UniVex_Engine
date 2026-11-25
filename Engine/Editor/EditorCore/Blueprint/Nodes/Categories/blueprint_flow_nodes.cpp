// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_flow_nodes.h"

#include <string>
#include <utility>
#include <vector>

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterFlowBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Role = BlueprintNodeBuilder::Role;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};
    const auto Exec = [](std::string name = "") { return BlueprintNodeBuilder::Exec(std::move(name)); };

    b.Flow("branch", "Branch", Role::Branch, {Exec(), Pin{"Condition", T::Bool, "false"}}, {Exec("True"), Exec("False")}, "if else", "One way or the other");
    b.Flow("sequence", "Sequence", Role::Sequence, {Exec()}, {Exec("Then 0"), Exec("Then 1"), Exec("Then 2"), Exec("Then 3")}, "then order", "Each output in turn");
    b.Flow("for_loop", "For Loop", Role::ForLoop, {Exec(), Pin{"First Index", T::Int, "0"}, Pin{"Last Index", T::Int, "9"}},
           {Exec("Loop Body"), Pin{"Index", T::Int, ""}, Exec("Completed")}, "repeat count", "From First to Last, inclusive");
    b.Flow("while_loop", "While Loop", Role::While, {Exec(), Pin{"Condition", T::Bool, "false"}}, {Exec("Loop Body"), Exec("Completed")}, "repeat", "While the condition holds");
    b.Flow("do_once", "Do Once", Role::DoOnce, {Exec()}, {Exec("Completed")}, "single first", "Only the first time");
    b.Flow("flip_flop", "Flip Flop", Role::FlipFlop, {Exec()}, {Exec("A"), Exec("B"), Pin{"Is A", T::Bool, ""}}, "toggle alternate", "A, then B, then A ...");
    b.Flow("delay", "Delay", Role::Delay, {Exec(), Pin{"Duration", T::Float, "0.5"}}, {Exec("Completed")}, "wait timer seconds", "Waits, then goes on");
    b.Flow("gate", "Gate", Role::Gate, {Exec("Enter"), Exec("Open"), Exec("Close"), Exec("Toggle"), Pin{"Start Closed", T::Bool, "false"}},
           {Exec("Exit")}, "open close door block", "Enter goes on only while the gate is open");
    b.Flow("multi_gate", "Multi Gate", Role::MultiGate, {Exec("Enter"), Exec("Reset"), Pin{"Loop", T::Bool, "true"}},
           {Exec("Out 0"), Exec("Out 1"), Exec("Out 2"), Exec("Out 3")}, "cycle round robin each next", "Each time, the next output");
    b.Flow("do_n", "Do N", Role::DoN, {Exec("Enter"), Pin{"N", T::Int, "3"}, Exec("Reset")}, {Exec("Exit"), Pin{"Counter", T::Int, ""}},
           "limit times count", "Goes on the first N times, until Reset");
    b.Flow("retriggerable_delay", "Retriggerable Delay", Role::RetriggerableDelay, {Exec(), Pin{"Duration", T::Float, "0.5"}}, {Exec("Completed")},
           "wait debounce restart timer", "Waits; entering again starts the wait over");
    b.Flow("switch_int", "Switch on Int", Role::SwitchInt, {Exec(), Pin{"Selection", T::Int, "0"}},
           {Exec("0"), Exec("1"), Exec("2"), Exec("3"), Exec("Default")}, "case select index", "The output of the number");
    b.Flow("switch_string", "Switch on String", Role::SwitchString,
           {Exec(), Pin{"Selection", T::String, ""}, Pin{"Case 0", T::String, "A"}, Pin{"Case 1", T::String, "B"}, Pin{"Case 2", T::String, "C"},
            Pin{"Case 3", T::String, "D"}},
           {Exec("Case 0"), Exec("Case 1"), Exec("Case 2"), Exec("Case 3"), Exec("Default")}, "case select name", "The output of the matching case");
    b.Flow("is_valid", "Is Valid", Role::IsValid, {Exec(), Pin{"Object", T::Object, ""}}, {Exec("Is Valid"), Exec("Is Not Valid")}, "none null exists", "Whether the object was found");

    const std::vector<std::pair<T, std::string>> arrays = {
        {T::ObjectArray, "Object"}, {T::IntArray, "Int"}, {T::FloatArray, "Float"}, {T::StringArray, "String"}};
    for (const auto& [arrayType, word] : arrays) {
        const T item = Asset::BlueprintArrayItemType(arrayType);
        const std::string suffix = " (" + word + " Array)";
        const std::string key = "_" + word;
        b.Flow("for_each" + key, "For Each" + suffix, Role::ForEach, {Exec(), Pin{"Array", arrayType, ""}},
               {Exec("Loop Body"), Pin{"Element", item, ""}, Pin{"Index", T::Int, ""}, Exec("Completed")}, "loop array list foreach every",
               "The Loop Body once for each item of the array");
    }
}

} // namespace UVE::Blueprint::Nodes
