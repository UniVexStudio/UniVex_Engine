// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_character_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterCharacterBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    b.Action("add_movement_input", "Add Movement Input", "Character", {Pin{"World Direction", T::Vector, "0,0,-1"}, Pin{"Scale", T::Float, "1.0"}},
             "character.move({1} * {2})", "walk run wasd move", "Steers the character this frame (with its speed, acceleration and air control)");
    b.Action("character_jump", "Jump", "Character", {}, "character.jump()", "hop leap", "Jumps when it can (on the floor, or within coyote time)");
    b.Action("character_launch", "Launch Character", "Character", {Pin{"Velocity", T::Vector, "0,10,0"}}, "character.launch({1})", "knockback push",
             "Adds to its velocity at once");
    b.Pure("character_is_falling", "Is Falling", "Character", {}, {Pin{"Falling", T::Bool, ""}}, {"character.falling()"}, "air midair");
    b.Pure("character_speed", "Get Speed", "Character", {}, {Pin{"Speed", T::Float, ""}}, {"character.speed()"}, "velocity ground",
           "Its speed along the ground, metres per second");
    b.Action("set_max_walk_speed", "Set Max Walk Speed", "Character", {Pin{"Speed", T::Float, "5.0"}}, "max_speed = {1}", "run sprint");
    b.Pure("get_max_walk_speed", "Get Max Walk Speed", "Character", {}, {Pin{"Speed", T::Float, ""}}, {"max_speed"});
    b.Action("set_jump_height", "Set Jump Height", "Character", {Pin{"Height", T::Float, "1.5"}}, "jump_height = {1}");
}

} // namespace UVE::Blueprint::Nodes
