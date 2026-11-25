// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_physics_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterPhysicsBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // Rigid body forces & character grounded query
    b.Action("apply_force", "Apply Force", "Physics", {Pin{"Force", T::Vector, "0,0,0"}}, "physics.apply_force({1})", "add push", "A rigid body");
    b.Action("apply_impulse", "Apply Impulse", "Physics", {Pin{"Impulse", T::Vector, "0,0,0"}}, "physics.apply_impulse({1})", "add kick", "A rigid body");
    b.Action("apply_torque", "Apply Torque", "Physics", {Pin{"Torque", T::Vector, "0,0,0"}}, "physics.apply_torque({1})", "spin rotate", "A rigid body");
    b.Pure("is_grounded", "Is Grounded", "Physics", {}, {Pin{"Grounded", T::Bool, ""}}, {"grounded"}, "on floor falling", "A character");

    // Ray & shape traces
    b.ValuedAction("line_trace", "Line Trace", "Physics|Traces", {Pin{"Start", T::Vector, "0,0,0"}, Pin{"End", T::Vector, "0,0,-10"}},
                   {Pin{"Hit", T::Bool, ""}, Pin{"Location", T::Vector, ""}, Pin{"Normal", T::Vector, ""}, Pin{"Hit Object", T::Object, ""},
                    Pin{"Distance", T::Float, ""}},
                   {"trace.line({1}, {2})", "trace.location()", "trace.normal()", "trace.object()", "trace.distance()"},
                   "raycast line trace by channel ray hit", "The first collider between Start and End (never this object)");
    b.ValuedAction("sphere_trace", "Sphere Trace", "Physics|Traces",
                   {Pin{"Start", T::Vector, "0,0,0"}, Pin{"End", T::Vector, "0,0,-10"}, Pin{"Radius", T::Float, "0.5"}},
                   {Pin{"Hit", T::Bool, ""}, Pin{"Location", T::Vector, ""}, Pin{"Normal", T::Vector, ""}, Pin{"Hit Object", T::Object, ""},
                    Pin{"Distance", T::Float, ""}},
                   {"trace.sphere({1}, {2}, {3})", "trace.location()", "trace.normal()", "trace.object()", "trace.distance()"},
                   "sweep shape cast sphere trace", "A ball moved from Start to End: the first collider it touches");
}

} // namespace UVE::Blueprint::Nodes
