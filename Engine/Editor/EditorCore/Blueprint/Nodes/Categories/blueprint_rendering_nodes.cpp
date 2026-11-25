// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_rendering_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterRenderingBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // Light & FOV
    b.Action("set_light_intensity", "Set Light Intensity", "Rendering", {Pin{"Intensity", T::Float, "1.0"}}, "intensity = {1}", "brightness energy");
    b.Pure("get_light_intensity", "Get Light Intensity", "Rendering", {}, {Pin{"Intensity", T::Float, ""}}, {"intensity"});
    b.Action("set_light_color", "Set Light Color", "Rendering", {Pin{"Color", T::Vector, "1,1,1"}}, "light_color = {1}", "colour tint");
    b.Action("set_field_of_view", "Set Field Of View", "Rendering", {Pin{"Degrees", T::Float, "60.0"}}, "fov = {1}", "zoom camera fov");
    b.Pure("get_field_of_view", "Get Field Of View", "Rendering", {}, {Pin{"Degrees", T::Float, ""}}, {"fov"}, "zoom camera fov");

    // Particle effects & Camera view
    b.Action("set_particles_active", "Set Particles Active", "Effects", {Pin{"Active", T::Bool, "true"}}, "particles.set_active({1})",
             "emitter vfx activate deactivate", "Starts or stops its Particle Emitter");
    b.Action("burst_particles", "Burst Particles", "Effects", {Pin{"Count", T::Int, "30"}}, "particles.burst({1})", "emitter vfx explosion spawn",
             "Lets out so many particles at once, on top of its rate");
    b.Action("set_active_camera", "Set View Target", "Camera", {Pin{"Camera", T::Object, ""}}, "camera.activate({1})",
             "camera switch view target blend", "Makes that camera the view");
    b.Action("camera_shake", "Camera Shake", "Camera", {Pin{"Strength", T::Float, "0.2"}, Pin{"Seconds", T::Float, "0.4"}}, "camera.shake({1}, {2})",
             "shake impact explosion hit", "Shakes the view, fading out");
    b.Pure("get_camera_forward", "Get Camera Forward", "Camera", {}, {Pin{"Forward", T::Vector, ""}}, {"camera.forward()"}, "view direction aim");
    b.Pure("get_camera_location", "Get Camera Location", "Camera", {}, {Pin{"Location", T::Vector, ""}}, {"camera.location()"}, "view position eye");

    // Surface material overrides
    b.Action("set_material", "Set Material", "Rendering|Material", {Pin{"Material", T::String, "Materials/Red.uvmat"}}, "render.set_material({1})",
             "material swap override change", "Draws every surface with that material (empty: its own again)");
    b.Action("set_transparency", "Set Transparency", "Rendering|Material", {Pin{"Transparency", T::Float, "0.5"}}, "render.set_transparency({1})",
             "fade opacity alpha see through", "0 solid, 1 invisible");
}

} // namespace UVE::Blueprint::Nodes
