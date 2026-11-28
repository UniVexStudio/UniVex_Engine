// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_ragdoll_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterRagdollBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};
    const std::string category = "Physics|Ragdoll";

    // Switching the body between animation and physics.
    b.Action("ragdoll_activate", "Activate Ragdoll", category, {}, "ragdoll.activate()",
             "go limp fall down simulate ragdoll", "The body goes limp and falls under physics");
    b.Action("ragdoll_activate_powered", "Activate Powered Ragdoll", category, {}, "ragdoll.activate_powered()",
             "active ragdoll motors physical animation hit reaction",
             "The body reacts to forces but its joints still try to hold the animated pose");
    b.Action("ragdoll_deactivate", "Deactivate Ragdoll", category, {}, "ragdoll.deactivate()",
             "animation takes over stop ragdoll", "Hands the skeleton straight back to the animation");
    b.Action("ragdoll_get_up", "Get Up", category, {}, "ragdoll.get_up()", "stand up recover wake",
             "The fallen body stands back up, blending into the animation");

    // Forces.
    b.Action("ragdoll_apply_impulse", "Ragdoll Impulse", category, {Pin{"Impulse", T::Vector, "0,0,0"}},
             "ragdoll.apply_impulse({1})", "shove push kick ragdoll", "A shove on the whole body, at the pelvis");
    b.Action("ragdoll_hit", "Ragdoll Hit", category, {Pin{"Point", T::Vector, "0,0,0"}, Pin{"Impulse", T::Vector, "0,0,0"}},
             "ragdoll.hit({1}, {2})", "hit bullet blast limb",
             "A hit at a world point: it lands on the body part nearest to Point");

    // Settings.
    b.Action("ragdoll_set_blend", "Set Ragdoll Blend", category, {Pin{"Blend", T::Float, "1"}},
             "ragdoll.set_blend({1})", "physics weight partial ragdoll",
             "0 is the animated pose, 1 is the full physical pose");
    b.Action("ragdoll_set_auto_recover", "Set Auto Recover", category, {Pin{"Enabled", T::Bool, "true"}},
             "ragdoll.set_auto_recover({1})", "stand up on its own", "Whether a resting body stands back up by itself");
    b.Action("ragdoll_set_motor_stiffness", "Set Ragdoll Motor Stiffness", category, {Pin{"Stiffness", T::Float, "160"}},
             "ragdoll.set_motor_stiffness({1})", "powered ragdoll strength",
             "How strongly a powered ragdoll's joints pull toward the animated pose");

    // Queries.
    b.Pure("ragdoll_is_active", "Is Ragdoll Active", category, {}, {Pin{"Active", T::Bool, ""}},
           {"ragdoll.is_active()"}, "simulating limp", "True while the body is under physics or standing back up");
    b.Pure("ragdoll_is_at_rest", "Is Ragdoll At Rest", category, {}, {Pin{"At Rest", T::Bool, ""}},
           {"ragdoll.is_at_rest()"}, "settled sleeping", "True once the fallen body has stopped moving");
    b.Pure("ragdoll_is_lying", "Is Ragdoll Lying Down", category, {}, {Pin{"Lying", T::Bool, ""}},
           {"ragdoll.is_lying()"}, "fallen prone supine", "True while the body lies face up or face down");
    b.Pure("ragdoll_get_blend", "Get Ragdoll Blend", category, {}, {Pin{"Blend", T::Float, ""}},
           {"ragdoll.get_blend()"}, "physics weight", "The current physics blend, 0 to 1");
}

} // namespace UVE::Blueprint::Nodes
