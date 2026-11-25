// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_transform_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterTransformBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // Position, scale, velocity, visibility, name
    b.Pure("get_position", "Get Position", "Transform", {}, {Pin{"Position", T::Vector, ""}}, {"position"}, "location actor");
    b.Action("set_position", "Set Position", "Transform", {Pin{"Position", T::Vector, "0,0,0"}}, "position = {1}", "location teleport move");
    b.Action("add_position", "Add Offset", "Transform", {Pin{"Offset", T::Vector, "0,0,0"}}, "position += {1}", "move translate");
    b.Pure("get_scale", "Get Scale", "Transform", {}, {Pin{"Scale", T::Vector, ""}}, {"scale"});
    b.Action("set_scale", "Set Scale", "Transform", {Pin{"Scale", T::Vector, "1,1,1"}}, "scale = {1}", "size");
    b.Pure("get_velocity", "Get Velocity", "Transform", {}, {Pin{"Velocity", T::Vector, ""}}, {"velocity"}, "speed", "A character or rigid body's velocity");
    b.Action("set_velocity", "Set Velocity", "Transform", {Pin{"Velocity", T::Vector, "0,0,0"}}, "velocity = {1}", "speed move", "A character or rigid body's velocity");
    b.Pure("get_visible", "Is Visible", "Transform", {}, {Pin{"Visible", T::Bool, ""}}, {"visible"});
    b.Action("set_visible", "Set Visible", "Transform", {Pin{"Visible", T::Bool, "true"}}, "visible = {1}", "hide show");
    b.Pure("get_name", "Get Name", "Transform", {}, {Pin{"Name", T::String, ""}}, {"name"}, "self");

    // Rotation and directions
    b.Pure("get_rotation", "Get Rotation", "Transform", {}, {Pin{"Rotation", T::Vector, ""}}, {"rotation"}, "euler angles", "Degrees: X pitch, Y yaw, Z roll");
    b.Action("set_rotation", "Set Rotation", "Transform", {Pin{"Rotation", T::Vector, "0,0,0"}}, "rotation = {1}", "euler angles turn");
    b.Action("add_yaw", "Add Yaw", "Transform", {Pin{"Degrees", T::Float, "0.0"}}, "transform.add_yaw({1})", "turn look horizontal", "Turns about the world's up");
    b.Action("add_pitch", "Add Pitch", "Transform", {Pin{"Degrees", T::Float, "0.0"}}, "transform.add_pitch({1})", "look vertical tilt");
    b.Action("look_at", "Look At", "Transform", {Pin{"Target", T::Vector, "0,0,0"}}, "transform.look_at({1})", "face aim", "Turns so it faces the point");
    b.Pure("get_forward_vector", "Get Forward Vector", "Transform", {}, {Pin{"Forward", T::Vector, ""}}, {"transform.forward()"}, "direction facing");
    b.Pure("get_right_vector", "Get Right Vector", "Transform", {}, {Pin{"Right", T::Vector, ""}}, {"transform.right()"}, "direction side");
    b.Pure("get_up_vector", "Get Up Vector", "Transform", {}, {Pin{"Up", T::Vector, ""}}, {"transform.up()"}, "direction");
    b.Pure("get_distance_to", "Get Distance To", "Transform", {Pin{"Target", T::Vector, "0,0,0"}}, {Pin{"Distance", T::Float, ""}},
           {"transform.distance_to({0})"}, "range far near");
}

} // namespace UVE::Blueprint::Nodes
