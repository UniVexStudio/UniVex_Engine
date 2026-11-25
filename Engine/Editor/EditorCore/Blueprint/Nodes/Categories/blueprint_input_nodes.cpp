// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_input_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterInputBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    b.Pure("input_pressed", "Is Action Pressed", "Input", {Pin{"Action", T::String, "Jump"}}, {Pin{"Pressed", T::Bool, ""}}, {"input.pressed({0})"}, "key button just");
    b.Pure("input_held", "Is Action Held", "Input", {Pin{"Action", T::String, "Jump"}}, {Pin{"Held", T::Bool, ""}}, {"input.held({0})"}, "key button down");
    b.Pure("input_released", "Was Action Released", "Input", {Pin{"Action", T::String, "Jump"}}, {Pin{"Released", T::Bool, ""}}, {"input.released({0})"}, "key button up");
    b.Pure("input_axis", "Get Axis", "Input", {Pin{"Negative", T::String, "left"}, Pin{"Positive", T::String, "right"}}, {Pin{"Value", T::Float, ""}},
           {"input.axis({0}, {1})"}, "move stick");
    b.Pure("input_axis_value", "Get Axis Value", "Input", {Pin{"Action", T::String, "MoveForward"}}, {Pin{"Value", T::Float, ""}},
           {"input.axis_value({0})"}, "move stick wasd forward right axis1d", "An Axis action from the Input Map (MoveForward, MoveHorizontal, LookHorizontal ...): -1 to 1");
    b.Pure("input_mouse_delta", "Get Mouse Delta", "Input", {}, {Pin{"X", T::Float, ""}, Pin{"Y", T::Float, ""}},
           {"input.mouse_delta().x", "input.mouse_delta().y"}, "look turn camera mouse", "How far the mouse moved this frame, in pixels");
}

} // namespace UVE::Blueprint::Nodes
