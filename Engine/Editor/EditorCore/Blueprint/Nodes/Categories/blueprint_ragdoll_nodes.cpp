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

    // The mode, as a choice of the four a ragdoll has.
    b.Action("ragdoll_set_mode", "Set Ragdoll Mode", category, {Pin{"Mode", T::String, "Simulating"}},
             "ragdoll.set_mode({1})", "kinematic active simulating blending out",
             "Kinematic: the animation drives. Active: powered ragdoll. Simulating: limp. BlendingOut: standing back up");
    b.Pure("ragdoll_get_mode", "Get Ragdoll Mode", category, {}, {Pin{"Mode", T::String, ""}},
           {"ragdoll.get_mode()"}, "current mode", "Kinematic, Active, Simulating or BlendingOut");
    b.Action("ragdoll_stagger", "Stagger", category, {Pin{"Strength", T::Float, "1"}}, "ragdoll.stagger({1})",
             "light hit stumble reaction", "The physics takes over for a moment, then the body returns to the animation");

    // Automatic reactions.
    b.Action("ragdoll_set_automatic_reactions", "Set Automatic Reactions", category, {Pin{"Enabled", T::Bool, "true"}},
             "ragdoll.set_automatic_reactions({1})", "react to hits and landings",
             "A light hit staggers the body, a strong hit or a hard landing knocks it down");
    b.Pure("ragdoll_get_automatic_reactions", "Get Automatic Reactions", category, {}, {Pin{"Enabled", T::Bool, ""}},
           {"ragdoll.get_automatic_reactions()"}, "reactions on off", "Whether hits and landings react on their own");
    b.Action("ragdoll_set_reaction", "Set Ragdoll Reaction Setting", category,
             {Pin{"Setting", T::String, "StaggerImpulse"}, Pin{"Value", T::Float, "0"}},
             "ragdoll.set_reaction({1}, {2})", "stagger fall threshold landing peak rise decay tilt",
             "StaggerImpulse, FallImpulse, FallLandingSpeed, StaggerPeak, StaggerRise, StaggerDecay or StaggerFallTilt");
    b.Pure("ragdoll_get_reaction", "Get Ragdoll Reaction Setting", category, {Pin{"Setting", T::String, "StaggerImpulse"}},
           {Pin{"Value", T::Float, ""}}, {"ragdoll.get_reaction({0})"}, "stagger fall threshold landing peak rise decay tilt",
           "One of the automatic reaction settings");

    // Queries.
    b.Pure("ragdoll_lying_orientation", "Get Ragdoll Lying Orientation", category, {}, {Pin{"Orientation", T::String, ""}},
           {"ragdoll.lying_orientation()"}, "face up face down prone supine",
           "Upright, FaceUp, FaceDown or Unknown");
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
