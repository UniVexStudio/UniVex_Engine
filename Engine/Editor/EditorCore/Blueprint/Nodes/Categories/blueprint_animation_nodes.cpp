// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_animation_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterAnimationBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // Motion Montage
    b.Action("montage_play", "Play Montage", "Animation", {Pin{"Montage", T::String, "Attack"}}, "montage.play({1})", "attack anim", "On its MontageSequencer, by the montage's name");
    b.Action("montage_play_section", "Play Montage Section", "Animation", {Pin{"Montage", T::String, "Attack"}, Pin{"Section", T::String, "Default"}},
             "montage.play_section({1}, {2})", "combo");
    b.Action("montage_jump_to", "Jump To Section", "Animation", {Pin{"Section", T::String, "Default"}}, "montage.jump_to({1})", "combo next");
    b.Action("montage_stop", "Stop Montage", "Animation", {}, "montage.stop()");
    b.Pure("montage_is_playing", "Is Montage Playing", "Animation", {}, {Pin{"Playing", T::Bool, ""}}, {"montage.is_playing()"});
    b.Pure("montage_section", "Current Section", "Animation", {}, {Pin{"Section", T::String, ""}}, {"montage.section()"});
    b.Pure("montage_curve", "Montage Curve", "Animation", {Pin{"Curve", T::String, ""}}, {Pin{"Value", T::Float, ""}}, {"montage.curve({0})"});

    // Animation Graph parameters
    b.Action("anim_set_float", "Set Anim Float", "Animation", {Pin{"Parameter", T::String, "Speed"}, Pin{"Value", T::Float, "0.0"}},
             "anim.set_float({1}, {2})", "animation graph parameter", "A Float parameter of its Animation Graph");
    b.Action("anim_set_bool", "Set Anim Bool", "Animation", {Pin{"Parameter", T::String, "IsJumping"}, Pin{"Value", T::Bool, "true"}},
             "anim.set_bool({1}, {2})", "animation graph parameter", "A Bool parameter of its Animation Graph");
    b.Action("anim_set_int", "Set Anim Int", "Animation", {Pin{"Parameter", T::String, "Weapon"}, Pin{"Value", T::Int, "0"}},
             "anim.set_int({1}, {2})", "animation graph parameter", "A whole-number parameter (kept as a Float)");
    b.Action("anim_trigger", "Fire Anim Trigger", "Animation", {Pin{"Parameter", T::String, "Jump"}}, "anim.trigger({1})",
             "animation graph trigger", "A Trigger parameter: set until a transition uses it");
    b.Action("anim_set_vector", "Set Anim Vector", "Animation",
             {Pin{"Parameter", T::String, "LookAt"}, Pin{"X", T::Float, "0.0"}, Pin{"Y", T::Float, "0.0"}, Pin{"Z", T::Float, "0.0"}},
             "anim.set_vector({1}, {2}, {3}, {4})", "animation graph ik target", "A Vector3 parameter (an IK or look target)");
    b.Pure("anim_get_float", "Get Anim Float", "Animation", {Pin{"Parameter", T::String, "Speed"}}, {Pin{"Value", T::Float, ""}},
           {"anim.get_float({0})"});
    b.Pure("anim_get_bool", "Get Anim Bool", "Animation", {Pin{"Parameter", T::String, "IsJumping"}}, {Pin{"Value", T::Bool, ""}},
           {"anim.get_bool({0})"});

    // Animation Sequencer playback
    b.Action("play_animation", "Play Animation", "Animation|Sequencer", {Pin{"Clip", T::String, ""}}, "sequencer.play({1})",
             "play clip anim sequence", "Plays a clip of its Animation Sequencer from the start (by name; empty: the one it has)");
    b.Action("stop_animation", "Stop Animation", "Animation|Sequencer", {}, "sequencer.stop()", "stop clip pause anim");
    b.Action("set_animation_rate", "Set Play Rate", "Animation|Sequencer", {Pin{"Rate", T::Float, "1.0"}}, "sequencer.set_rate({1})",
             "speed play rate reverse");
    b.Pure("is_animation_playing", "Is Animation Playing", "Animation|Sequencer", {}, {Pin{"Playing", T::Bool, ""}}, {"sequencer.is_playing()"});
}

} // namespace UVE::Blueprint::Nodes
