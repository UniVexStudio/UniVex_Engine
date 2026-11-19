// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_system_asset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "logging_macros.h"
#include "uve_file_envelope.h"

namespace UVE::Asset {
namespace {

using JsonUVE = nlohmann::json;

constexpr std::string_view kParticleSystemSchemaUVE = "uve-particle-system-v1";
constexpr std::size_t kMaximumParticleMathNodesUVE = 256U;
constexpr std::size_t kMaximumParticleCommentsUVE = 256U;

[[nodiscard]] ParticleFloatInputUVE MakeFloatUVE(const float value) noexcept {
    ParticleFloatInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleFloatInputUVE MakeUnitCurveFloatUVE() {
    ParticleFloatInputUVE input;
    input.kind = ParticleValueKindUVE::Curve;
    input.scale = 1.0F;
    input.curve.smooth = true;
    input.curve.keys = {{0.0F, 1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F, 0.0F}};
    return input;
}

[[nodiscard]] ParticleVectorInputUVE MakeVectorUVE(const Math::Vector3UVE& value) noexcept {
    ParticleVectorInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleColorInputUVE MakeColorUVE(const Math::ColorUVE& value, const float alpha = 1.0F) noexcept {
    ParticleColorInputUVE input;
    input.kind = ParticleValueKindUVE::Constant;
    input.constant = value;
    input.alpha = alpha;
    return input;
}

[[nodiscard]] ParticleColorInputUVE MakeWhiteGradientColorUVE() {
    ParticleColorInputUVE input;
    input.kind = ParticleValueKindUVE::Gradient;
    input.gradient.stops = {
        {0.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
        {1.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
    };
    return input;
}

[[nodiscard]] std::vector<ParticleModuleDescriptorUVE> BuildModuleCatalogUVE() {
    std::vector<ParticleModuleDescriptorUVE> catalog;
    catalog.reserve(46U);

    // ---- Emitter Update ----
    catalog.push_back({
        .type = ParticleModuleTypeUVE::SpawnRate,
        .stage = ParticleStageUVE::EmitterUpdate,
        .canonicalName = "SpawnRate",
        .displayName = "Spawn Rate",
        .description = "Emits particles continuously at a rate per second.",
        .defaultFloats = {{"Rate", MakeFloatUVE(10.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::SpawnBurst,
        .stage = ParticleStageUVE::EmitterUpdate,
        .canonicalName = "SpawnBurst",
        .displayName = "Spawn Burst",
        .description = "Emits bursts of particles at scheduled times in each loop.",
        .defaultFloats = {{"Time", MakeFloatUVE(0.0F)}, {"Interval", MakeFloatUVE(0.0F)}},
        .defaultInts = {{"Count", 10}, {"Cycles", 1}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::SpawnPerDistance,
        .stage = ParticleStageUVE::EmitterUpdate,
        .canonicalName = "SpawnPerDistance",
        .displayName = "Spawn Per Distance",
        .description = "Emits particles proportionally to emitter movement distance.",
        .defaultFloats = {{"PerUnit", MakeFloatUVE(1.0F)}},
    });

    // ---- Particle Spawn ----
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ShapePoint,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "ShapePoint",
        .displayName = "Shape: Point",
        .description = "Spawns particles at the emitter origin.",
        .defaultFloats = {{"NormalSpeed", MakeFloatUVE(0.0F)}},
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ShapeSphere,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "ShapeSphere",
        .displayName = "Shape: Sphere",
        .description = "Spawns particles inside or on the surface of a sphere or hemisphere.",
        .defaultFloats = {{"Radius", MakeFloatUVE(1.0F)}, {"NormalSpeed", MakeFloatUVE(0.0F)}},
        .defaultInts = {{"Surface", 0}, {"Hemisphere", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ShapeBox,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "ShapeBox",
        .displayName = "Shape: Box",
        .description = "Spawns particles inside or on the surface of a box.",
        .defaultFloats = {{"NormalSpeed", MakeFloatUVE(0.0F)}},
        .defaultVectors = {{"Size", MakeVectorUVE({1.0F, 1.0F, 1.0F})}},
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ShapeCone,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "ShapeCone",
        .displayName = "Shape: Cone",
        .description = "Spawns particles inside or on the surface of a cone.",
        .defaultFloats = {
            {"Angle", MakeFloatUVE(25.0F)},
            {"Length", MakeFloatUVE(1.0F)},
            {"Radius", MakeFloatUVE(0.2F)},
            {"NormalSpeed", MakeFloatUVE(0.0F)},
        },
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ShapeCylinder,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "ShapeCylinder",
        .displayName = "Shape: Cylinder",
        .description = "Spawns particles inside or on the surface of a cylinder.",
        .defaultFloats = {
            {"Radius", MakeFloatUVE(0.5F)},
            {"Height", MakeFloatUVE(1.0F)},
            {"NormalSpeed", MakeFloatUVE(0.0F)},
        },
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ShapeTorus,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "ShapeTorus",
        .displayName = "Shape: Torus",
        .description = "Spawns particles inside or on the surface of a torus.",
        .defaultFloats = {
            {"MajorRadius", MakeFloatUVE(1.0F)},
            {"MinorRadius", MakeFloatUVE(0.25F)},
            {"NormalSpeed", MakeFloatUVE(0.0F)},
        },
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ShapeMeshSurface,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "ShapeMeshSurface",
        .displayName = "Shape: Mesh Surface",
        .description = "Spawns particles uniformly across the triangles of a mesh.",
        .defaultFloats = {{"NormalSpeed", MakeFloatUVE(0.0F)}},
        .defaultInts = {{"Surface", 1}},
        .defaultStrings = {{"Mesh", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialVelocity,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialVelocity",
        .displayName = "Initial Velocity",
        .description = "Sets each particle's starting velocity vector.",
        .defaultVectors = {{"Velocity", MakeVectorUVE({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::VelocityInCone,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "VelocityInCone",
        .displayName = "Velocity In Cone",
        .description = "Launches particles within a cone around an axis.",
        .defaultFloats = {{"Angle", MakeFloatUVE(25.0F)}, {"Speed", MakeFloatUVE(1.0F)}},
        .defaultVectors = {{"Axis", MakeVectorUVE({0.0F, 1.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialLifetime,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialLifetime",
        .displayName = "Initial Lifetime",
        .description = "Sets how many seconds each particle stays alive.",
        .defaultFloats = {{"Lifetime", MakeFloatUVE(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialColor,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialColor",
        .displayName = "Initial Color",
        .description = "Sets each particle's initial linear color and opacity.",
        .defaultColors = {{"Color", MakeColorUVE({1.0F, 1.0F, 1.0F}, 1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialSize,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialSize",
        .displayName = "Initial Size",
        .description = "Sets each particle's starting uniform or 2D size.",
        .defaultFloats = {{"Size", MakeFloatUVE(1.0F)}},
        .defaultVectors = {{"Size", MakeVectorUVE({1.0F, 1.0F, 1.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialRotation,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialRotation",
        .displayName = "Initial Rotation",
        .description = "Sets each particle's starting angle in degrees.",
        .defaultFloats = {{"Rotation", MakeFloatUVE(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialSpin,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialSpin",
        .displayName = "Initial Spin",
        .description = "Sets each particle's starting angular velocity in degrees per second.",
        .defaultFloats = {{"Spin", MakeFloatUVE(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialSpriteFrame,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialSpriteFrame",
        .displayName = "Initial Sprite Frame",
        .description = "Sets each particle's starting sprite flipbook frame index.",
        .defaultFloats = {{"Frame", MakeFloatUVE(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InitialMass,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InitialMass",
        .displayName = "Initial Mass",
        .description = "Sets each particle's physical mass.",
        .defaultFloats = {{"Mass", MakeFloatUVE(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::InheritOwnerVelocity,
        .stage = ParticleStageUVE::ParticleSpawn,
        .canonicalName = "InheritOwnerVelocity",
        .displayName = "Inherit Owner Velocity",
        .description = "Adds a scaled fraction of the emitter owner's velocity on spawn.",
        .defaultFloats = {{"Scale", MakeFloatUVE(1.0F)}},
    });

    // ---- Particle Update ----
    catalog.push_back({
        .type = ParticleModuleTypeUVE::Gravity,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "Gravity",
        .displayName = "Gravity",
        .description = "Applies constant acceleration such as gravity each frame.",
        .defaultVectors = {{"Acceleration", MakeVectorUVE({0.0F, -9.8F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::Drag,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "Drag",
        .displayName = "Drag",
        .description = "Slows particles proportionally to their current velocity.",
        .defaultFloats = {{"Coefficient", MakeFloatUVE(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::AddVelocity,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "AddVelocity",
        .displayName = "Add Velocity",
        .description = "Applies a steady directional push per second to live velocities.",
        .defaultVectors = {{"Velocity", MakeVectorUVE({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::CurlNoise,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "CurlNoise",
        .displayName = "Curl Noise",
        .description = "Adds divergence-free turbulent acceleration from 3D curl noise.",
        .defaultFloats = {
            {"Strength", MakeFloatUVE(1.0F)},
            {"Frequency", MakeFloatUVE(1.0F)},
            {"Speed", MakeFloatUVE(0.5F)},
        },
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::Vortex,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "Vortex",
        .displayName = "Vortex",
        .description = "Swirls particles tangentially around an axis and center point.",
        .defaultFloats = {{"Strength", MakeFloatUVE(2.0F)}, {"PullIn", MakeFloatUVE(0.0F)}},
        .defaultVectors = {
            {"Axis", MakeVectorUVE({0.0F, 1.0F, 0.0F})},
            {"Center", MakeVectorUVE({0.0F, 0.0F, 0.0F})},
        },
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::PointAttractor,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "PointAttractor",
        .displayName = "Point Attractor",
        .description = "Pulls particles toward a point within a radius and optionally removes close hits.",
        .defaultFloats = {
            {"Strength", MakeFloatUVE(5.0F)},
            {"Radius", MakeFloatUVE(5.0F)},
            {"KillRadius", MakeFloatUVE(0.0F)},
        },
        .defaultVectors = {{"Position", MakeVectorUVE({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::Wind,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "Wind",
        .displayName = "Wind",
        .description = "Steers particle velocities toward a wind direction with turbulence.",
        .defaultFloats = {{"Turbulence", MakeFloatUVE(0.0F)}},
        .defaultVectors = {{"Velocity", MakeVectorUVE({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::LimitSpeed,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "LimitSpeed",
        .displayName = "Limit Speed",
        .description = "Damps particle speed when velocity exceeds the maximum threshold.",
        .defaultFloats = {{"MaxSpeed", MakeFloatUVE(10.0F)}, {"Damping", MakeFloatUVE(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::Orbit,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "Orbit",
        .displayName = "Orbit",
        .description = "Moves particles along a circular orbit around their spawn point.",
        .defaultFloats = {{"Radius", MakeFloatUVE(1.0F)}, {"Speed", MakeFloatUVE(1.0F)}},
        .defaultVectors = {{"Axis", MakeVectorUVE({0.0F, 1.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::ColorOverLife,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "ColorOverLife",
        .displayName = "Color Over Life",
        .description = "Multiplies particle color over normalized lifetime with a gradient.",
        .defaultColors = {{"Color", MakeWhiteGradientColorUVE()}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::SizeOverLife,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "SizeOverLife",
        .displayName = "Size Over Life",
        .description = "Scales particle size over normalized lifetime with a curve.",
        .defaultFloats = {{"Scale", MakeUnitCurveFloatUVE()}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::RotationRate,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "RotationRate",
        .displayName = "Rotation Rate",
        .description = "Drives particle angular spin in degrees per second.",
        .defaultFloats = {{"Rate", MakeFloatUVE(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::SizeBySpeed,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "SizeBySpeed",
        .displayName = "Size By Speed",
        .description = "Remaps particle speed smoothly to a size scale range.",
        .defaultFloats = {
            {"MinSpeed", MakeFloatUVE(0.0F)},
            {"MaxSpeed", MakeFloatUVE(10.0F)},
            {"MinScale", MakeFloatUVE(1.0F)},
            {"MaxScale", MakeFloatUVE(1.0F)},
        },
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::SpriteFlipbook,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "SpriteFlipbook",
        .displayName = "Sprite Flipbook",
        .description = "Advances sprite sheet frame index over age or at a fixed frame rate.",
        .defaultFloats = {{"Fps", MakeFloatUVE(12.0F)}},
        .defaultInts = {{"Rows", 1}, {"Columns", 1}, {"Mode", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::CollisionPlane,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "CollisionPlane",
        .displayName = "Collision: Plane",
        .description = "Bounces particles against an infinite plane with friction and optional removal.",
        .defaultFloats = {
            {"Bounce", MakeFloatUVE(0.4F)},
            {"Friction", MakeFloatUVE(0.1F)},
            {"LifeLoss", MakeFloatUVE(0.0F)},
        },
        .defaultVectors = {
            {"Point", MakeVectorUVE({0.0F, 0.0F, 0.0F})},
            {"Normal", MakeVectorUVE({0.0F, 1.0F, 0.0F})},
        },
        .defaultInts = {{"KillOnHit", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::CollisionDepth,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "CollisionDepth",
        .displayName = "Collision: Depth",
        .description = "Collides GPU particles against the scene depth buffer.",
        .defaultFloats = {
            {"Bounce", MakeFloatUVE(0.4F)},
            {"Friction", MakeFloatUVE(0.1F)},
            {"LifeLoss", MakeFloatUVE(0.0F)},
        },
        .defaultVectors = {
            {"Point", MakeVectorUVE({0.0F, 0.0F, 0.0F})},
            {"Normal", MakeVectorUVE({0.0F, 1.0F, 0.0F})},
        },
        .defaultInts = {{"KillOnHit", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::CollisionPhysics,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "CollisionPhysics",
        .displayName = "Collision: Physics",
        .description = "Traces CPU particle motion rays against scene collision shapes.",
        .defaultFloats = {
            {"Bounce", MakeFloatUVE(0.4F)},
            {"Friction", MakeFloatUVE(0.1F)},
            {"LifeLoss", MakeFloatUVE(0.0F)},
        },
        .defaultVectors = {
            {"Point", MakeVectorUVE({0.0F, 0.0F, 0.0F})},
            {"Normal", MakeVectorUVE({0.0F, 1.0F, 0.0F})},
        },
        .defaultInts = {{"KillOnHit", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::KillByAge,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "KillByAge",
        .displayName = "Kill By Age",
        .description = "Removes particles once their age reaches their lifetime.",
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::KillByBounds,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "KillByBounds",
        .displayName = "Kill By Bounds",
        .description = "Removes particles that leave or enter an axis-aligned box.",
        .defaultVectors = {
            {"Min", MakeVectorUVE({-10.0F, -10.0F, -10.0F})},
            {"Max", MakeVectorUVE({10.0F, 10.0F, 10.0F})},
        },
        .defaultInts = {{"Inside", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::KillByCondition,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "KillByCondition",
        .displayName = "Kill By Condition",
        .description = "Removes particles whenever the wired condition evaluates above 0.5.",
        .defaultFloats = {{"Condition", MakeFloatUVE(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::EventOnDeath,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "EventOnDeath",
        .displayName = "Event On Death",
        .description = "Emits spawn events into another emitter when particles die.",
        .defaultInts = {{"Count", 1}},
        .defaultStrings = {{"TargetEmitter", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::EventOnCollision,
        .stage = ParticleStageUVE::ParticleUpdate,
        .canonicalName = "EventOnCollision",
        .displayName = "Event On Collision",
        .description = "Emits spawn events into another emitter when particles collide.",
        .defaultInts = {{"Count", 1}},
        .defaultStrings = {{"TargetEmitter", ""}},
    });

    // ---- Render ----
    catalog.push_back({
        .type = ParticleModuleTypeUVE::SpriteRenderer,
        .stage = ParticleStageUVE::Render,
        .canonicalName = "SpriteRenderer",
        .displayName = "Sprite Renderer",
        .description = "Draws instanced billboard or aligned quads for each particle.",
        .defaultFloats = {
            {"StretchScale", MakeFloatUVE(1.0F)},
            {"AlphaCutoff", MakeFloatUVE(0.5F)},
            {"SoftDistance", MakeFloatUVE(0.0F)},
            {"CameraFadeNear", MakeFloatUVE(0.0F)},
            {"CameraFadeFar", MakeFloatUVE(0.0F)},
            {"Emissive", MakeFloatUVE(1.0F)},
        },
        .defaultVectors = {{"Axis", MakeVectorUVE({0.0F, 1.0F, 0.0F})}},
        .defaultInts = {
            {"Facing", 0},
            {"Blend", 0},
            {"Rows", 1},
            {"Columns", 1},
            {"FrameBlend", 0},
        },
        .defaultStrings = {{"Texture", ""}, {"Material", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::RibbonRenderer,
        .stage = ParticleStageUVE::Render,
        .canonicalName = "RibbonRenderer",
        .displayName = "Ribbon Renderer",
        .description = "Draws camera-facing ribbon trails connecting particles or position histories.",
        .defaultFloats = {
            {"Width", MakeFloatUVE(0.1F)},
            {"TileLength", MakeFloatUVE(1.0F)},
            {"Emissive", MakeFloatUVE(1.0F)},
        },
        .defaultInts = {
            {"PerParticle", 0},
            {"TrailLength", 16},
            {"Mode", 0},
            {"Blend", 0},
        },
        .defaultStrings = {{"Texture", ""}, {"Material", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::MeshRenderer,
        .stage = ParticleStageUVE::Render,
        .canonicalName = "MeshRenderer",
        .displayName = "Mesh Renderer",
        .description = "Draws an instanced 3D mesh per live particle.",
        .defaultVectors = {{"Axis", MakeVectorUVE({0.0F, 1.0F, 0.0F})}},
        .defaultInts = {{"Orient", 0}},
        .defaultStrings = {{"Mesh", ""}, {"Material", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleTypeUVE::LightRenderer,
        .stage = ParticleStageUVE::Render,
        .canonicalName = "LightRenderer",
        .displayName = "Light Renderer",
        .description = "Emits dynamic point lights from particles within the renderer budget.",
        .defaultFloats = {{"Radius", MakeFloatUVE(2.0F)}, {"Intensity", MakeFloatUVE(1.0F)}},
    });

    return catalog;
}

[[nodiscard]] bool NameOkUVE(const std::string_view name) noexcept {
    return !name.empty() && name.size() <= kMaximumParticleNameBytesUVE && !name.contains('\0');
}

[[nodiscard]] bool ColorFiniteUVE(const Math::ColorUVE& color) noexcept {
    return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b);
}

[[nodiscard]] bool CurveOkUVE(const ParticleCurveUVE& curve) noexcept {
    if (curve.keys.size() > kMaximumCurveKeysUVE) {
        return false;
    }
    float previousTime = -1.0F;
    for (std::size_t index = 0U; index < curve.keys.size(); ++index) {
        const ParticleCurveKeyUVE& key = curve.keys[index];
        if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.inTangent) ||
            !std::isfinite(key.outTangent) || key.time < 0.0F || key.time > 1.0F) {
            return false;
        }
        if (index > 0U && key.time < previousTime) {
            return false;
        }
        previousTime = key.time;
    }
    return true;
}

[[nodiscard]] bool GradientOkUVE(const ParticleGradientUVE& gradient) noexcept {
    if (gradient.stops.size() > kMaximumGradientStopsUVE) {
        return false;
    }
    float previousTime = -1.0F;
    for (std::size_t index = 0U; index < gradient.stops.size(); ++index) {
        const ParticleGradientStopUVE& stop = gradient.stops[index];
        if (!std::isfinite(stop.time) || !ColorFiniteUVE(stop.color) || !std::isfinite(stop.alpha) ||
            stop.time < 0.0F || stop.time > 1.0F) {
            return false;
        }
        if (index > 0U && stop.time < previousTime) {
            return false;
        }
        previousTime = stop.time;
    }
    return true;
}

[[nodiscard]] const ParticleUserParameterUVE* FindParameterUVE(
    const std::vector<ParticleUserParameterUVE>& parameters,
    const std::string_view name) noexcept {
    for (const ParticleUserParameterUVE& param : parameters) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

[[nodiscard]] bool ValidateFloatInputUVE(const ParticleFloatInputUVE& input,
                                         const std::vector<ParticleUserParameterUVE>& parameters) noexcept {
    if (input.kind != ParticleValueKindUVE::Constant && input.kind != ParticleValueKindUVE::RandomRange &&
        input.kind != ParticleValueKindUVE::Curve && input.kind != ParticleValueKindUVE::UserParameter &&
        input.kind != ParticleValueKindUVE::Wire) {
        return false;
    }
    if (!std::isfinite(input.constant) || !std::isfinite(input.min) || !std::isfinite(input.max) ||
        !std::isfinite(input.scale) || input.parameter.contains('\0')) {
        return false;
    }
    if (input.kind == ParticleValueKindUVE::RandomRange && input.min > input.max) {
        return false;
    }
    if (!CurveOkUVE(input.curve)) {
        return false;
    }
    if (input.kind == ParticleValueKindUVE::UserParameter) {
        const ParticleUserParameterUVE* param = FindParameterUVE(parameters, input.parameter);
        if (param == nullptr || param->type != ParticleUserParameterTypeUVE::Float) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateVectorInputUVE(const ParticleVectorInputUVE& input,
                                          const std::vector<ParticleUserParameterUVE>& parameters) noexcept {
    if (input.kind != ParticleValueKindUVE::Constant && input.kind != ParticleValueKindUVE::RandomRange &&
        input.kind != ParticleValueKindUVE::UserParameter && input.kind != ParticleValueKindUVE::Wire) {
        return false;
    }
    if (!Math::IsFiniteUVE(input.constant) || !Math::IsFiniteUVE(input.min) || !Math::IsFiniteUVE(input.max) ||
        input.parameter.contains('\0')) {
        return false;
    }
    if (input.kind == ParticleValueKindUVE::RandomRange &&
        (input.min.x > input.max.x || input.min.y > input.max.y || input.min.z > input.max.z)) {
        return false;
    }
    if (input.kind == ParticleValueKindUVE::UserParameter) {
        const ParticleUserParameterUVE* param = FindParameterUVE(parameters, input.parameter);
        if (param == nullptr || param->type != ParticleUserParameterTypeUVE::Vector) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateColorInputUVE(const ParticleColorInputUVE& input,
                                         const std::vector<ParticleUserParameterUVE>& parameters) noexcept {
    if (input.kind != ParticleValueKindUVE::Constant && input.kind != ParticleValueKindUVE::RandomRange &&
        input.kind != ParticleValueKindUVE::Gradient && input.kind != ParticleValueKindUVE::UserParameter) {
        return false;
    }
    if (!ColorFiniteUVE(input.constant) || !std::isfinite(input.alpha) ||
        !ColorFiniteUVE(input.minColor) || !ColorFiniteUVE(input.maxColor) ||
        input.parameter.contains('\0')) {
        return false;
    }
    if (!GradientOkUVE(input.gradient)) {
        return false;
    }
    if (input.kind == ParticleValueKindUVE::UserParameter) {
        const ParticleUserParameterUVE* param = FindParameterUVE(parameters, input.parameter);
        if (param == nullptr || param->type != ParticleUserParameterTypeUVE::Color) {
            return false;
        }
    }
    return true;
}

/// `expectedStage` empty: any stage is accepted (the detached list mixes stages).
[[nodiscard]] bool ValidateStageModulesUVE(const std::vector<ParticleModuleUVE>& modules,
                                           const std::optional<ParticleStageUVE> expectedStage,
                                           const std::vector<ParticleUserParameterUVE>& parameters,
                                           const std::size_t maximumCount = kMaximumParticleModulesPerStageUVE) noexcept {
    if (modules.size() > maximumCount) {
        return false;
    }
    for (const ParticleModuleUVE& mod : modules) {
        const ParticleModuleDescriptorUVE* desc = FindParticleModuleDescriptorUVE(mod.type);
        if (desc == nullptr || (expectedStage.has_value() && desc->stage != *expectedStage)) {
            return false;
        }
        if (!std::isfinite(mod.graphX) || !std::isfinite(mod.graphY)) {
            return false;
        }
        for (const auto& [name, val] : mod.floats) {
            if (!desc->defaultFloats.contains(name) || !ValidateFloatInputUVE(val, parameters)) {
                return false;
            }
        }
        for (const auto& [name, val] : mod.vectors) {
            if (!desc->defaultVectors.contains(name) || !ValidateVectorInputUVE(val, parameters)) {
                return false;
            }
        }
        for (const auto& [name, val] : mod.colors) {
            if (!desc->defaultColors.contains(name) || !ValidateColorInputUVE(val, parameters)) {
                return false;
            }
        }
        for (const auto& [name, val] : mod.ints) {
            static_cast<void>(val);
            if (!desc->defaultInts.contains(name)) {
                return false;
            }
        }
        for (const auto& [name, val] : mod.strings) {
            if (!desc->defaultStrings.contains(name) || val.contains('\0') || val.size() > 512U) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::string_view ParameterTypeToJsonUVE(const ParticleUserParameterTypeUVE type) noexcept {
    switch (type) {
        case ParticleUserParameterTypeUVE::Float: return "float";
        case ParticleUserParameterTypeUVE::Vector: return "vector";
        case ParticleUserParameterTypeUVE::Color: return "color";
        case ParticleUserParameterTypeUVE::Bool: return "bool";
        case ParticleUserParameterTypeUVE::Int: return "int";
    }
    return "float";
}

[[nodiscard]] bool TryParseParameterTypeUVE(const std::string_view text,
                                            ParticleUserParameterTypeUVE& outType) noexcept {
    if (text == "float") {
        outType = ParticleUserParameterTypeUVE::Float;
        return true;
    }
    if (text == "vector") {
        outType = ParticleUserParameterTypeUVE::Vector;
        return true;
    }
    if (text == "color") {
        outType = ParticleUserParameterTypeUVE::Color;
        return true;
    }
    if (text == "bool") {
        outType = ParticleUserParameterTypeUVE::Bool;
        return true;
    }
    if (text == "int") {
        outType = ParticleUserParameterTypeUVE::Int;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view SpaceToJsonUVE(const ParticleSimSpaceUVE space) noexcept {
    return space == ParticleSimSpaceUVE::Local ? "local" : "world";
}

[[nodiscard]] bool TryParseSpaceUVE(const std::string_view text, ParticleSimSpaceUVE& outSpace) noexcept {
    if (text == "world") {
        outSpace = ParticleSimSpaceUVE::World;
        return true;
    }
    if (text == "local") {
        outSpace = ParticleSimSpaceUVE::Local;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view TargetToJsonUVE(const ParticleSimTargetUVE target) noexcept {
    switch (target) {
        case ParticleSimTargetUVE::Cpu: return "cpu";
        case ParticleSimTargetUVE::Gpu: return "gpu";
        case ParticleSimTargetUVE::Auto: return "auto";
    }
    return "cpu";
}

[[nodiscard]] bool TryParseTargetUVE(const std::string_view text, ParticleSimTargetUVE& outTarget) noexcept {
    if (text == "cpu") {
        outTarget = ParticleSimTargetUVE::Cpu;
        return true;
    }
    if (text == "gpu") {
        outTarget = ParticleSimTargetUVE::Gpu;
        return true;
    }
    if (text == "auto") {
        outTarget = ParticleSimTargetUVE::Auto;
        return true;
    }
    return false;
}

[[nodiscard]] JsonUVE SerializeVec3UVE(const Math::Vector3UVE& v) {
    return JsonUVE::array({v.x, v.y, v.z});
}

[[nodiscard]] bool TryParseVec3UVE(const JsonUVE& j, Math::Vector3UVE& out) {
    if (!j.is_array() || j.size() != 3U) {
        return false;
    }
    out = Math::Vector3UVE{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    return true;
}

[[nodiscard]] JsonUVE SerializeFloatInputUVE(const ParticleFloatInputUVE& input) {
    JsonUVE j = JsonUVE::object();
    switch (input.kind) {
        case ParticleValueKindUVE::Constant:
            j["constant"] = input.constant;
            break;
        case ParticleValueKindUVE::RandomRange:
            j["random"] = JsonUVE::array({input.min, input.max});
            break;
        case ParticleValueKindUVE::Curve: {
            JsonUVE keys = JsonUVE::array();
            JsonUVE tangents = JsonUVE::array();
            bool hasTangents = false;
            for (const ParticleCurveKeyUVE& k : input.curve.keys) {
                keys.push_back(JsonUVE::array({k.time, k.value}));
                tangents.push_back(JsonUVE::array({k.inTangent, k.outTangent}));
                if (k.inTangent != 0.0F || k.outTangent != 0.0F) {
                    hasTangents = true;
                }
            }
            j["curve"] = std::move(keys);
            j["smooth"] = input.curve.smooth;
            if (hasTangents) {
                j["tangents"] = std::move(tangents);
            }
            if (input.scale != 1.0F) {
                j["scale"] = input.scale;
            }
            break;
        }
        case ParticleValueKindUVE::UserParameter:
            j["parameter"] = input.parameter;
            if (input.scale != 1.0F) {
                j["scale"] = input.scale;
            }
            break;
        case ParticleValueKindUVE::Wire:
            j["wire"] = input.wireNode;
            if (input.scale != 1.0F) {
                j["scale"] = input.scale;
            }
            break;
        case ParticleValueKindUVE::Gradient:
            break;
    }
    return j;
}

[[nodiscard]] bool TryParseFloatInputUVE(const JsonUVE& j, ParticleFloatInputUVE& out) {
    if (!j.is_object()) {
        return false;
    }
    out = ParticleFloatInputUVE{};
    if (j.contains("constant")) {
        if (j.size() != 1U) {
            return false;
        }
        out.kind = ParticleValueKindUVE::Constant;
        out.constant = j.at("constant").get<float>();
        return true;
    }
    if (j.contains("random")) {
        if (j.size() != 1U || !j.at("random").is_array() || j.at("random").size() != 2U) {
            return false;
        }
        out.kind = ParticleValueKindUVE::RandomRange;
        out.min = j.at("random")[0].get<float>();
        out.max = j.at("random")[1].get<float>();
        return true;
    }
    if (j.contains("curve")) {
        out.kind = ParticleValueKindUVE::Curve;
        for (const auto& item : j.items()) {
            const std::string& k = item.key();
            if (k != "curve" && k != "smooth" && k != "tangents" && k != "scale") {
                return false;
            }
        }
        const JsonUVE& keys = j.at("curve");
        if (!keys.is_array() || keys.size() > kMaximumCurveKeysUVE) {
            return false;
        }
        out.curve.smooth = j.value("smooth", true);
        out.scale = j.value("scale", 1.0F);
        out.curve.keys.reserve(keys.size());
        for (const JsonUVE& k : keys) {
            if (!k.is_array() || k.size() != 2U) {
                return false;
            }
            out.curve.keys.push_back(ParticleCurveKeyUVE{k[0].get<float>(), k[1].get<float>(), 0.0F, 0.0F});
        }
        if (j.contains("tangents")) {
            const JsonUVE& tangents = j.at("tangents");
            if (!tangents.is_array() || tangents.size() != out.curve.keys.size()) {
                return false;
            }
            for (std::size_t idx = 0U; idx < tangents.size(); ++idx) {
                if (!tangents[idx].is_array() || tangents[idx].size() != 2U) {
                    return false;
                }
                out.curve.keys[idx].inTangent = tangents[idx][0].get<float>();
                out.curve.keys[idx].outTangent = tangents[idx][1].get<float>();
            }
        }
        return true;
    }
    if (j.contains("parameter")) {
        for (const auto& item : j.items()) {
            if (item.key() != "parameter" && item.key() != "scale") {
                return false;
            }
        }
        out.kind = ParticleValueKindUVE::UserParameter;
        out.parameter = j.at("parameter").get<std::string>();
        out.scale = j.value("scale", 1.0F);
        return true;
    }
    if (j.contains("wire")) {
        for (const auto& item : j.items()) {
            if (item.key() != "wire" && item.key() != "scale") {
                return false;
            }
        }
        out.kind = ParticleValueKindUVE::Wire;
        out.wireNode = j.at("wire").get<std::uint32_t>();
        out.scale = j.value("scale", 1.0F);
        return true;
    }
    return false;
}

[[nodiscard]] JsonUVE SerializeVectorInputUVE(const ParticleVectorInputUVE& input) {
    JsonUVE j = JsonUVE::object();
    switch (input.kind) {
        case ParticleValueKindUVE::Constant:
            j["constant"] = SerializeVec3UVE(input.constant);
            break;
        case ParticleValueKindUVE::RandomRange:
            j["random"] = JsonUVE::array({SerializeVec3UVE(input.min), SerializeVec3UVE(input.max)});
            break;
        case ParticleValueKindUVE::UserParameter:
            j["parameter"] = input.parameter;
            break;
        case ParticleValueKindUVE::Wire:
            j["wire"] = input.wireNode;
            break;
        case ParticleValueKindUVE::Curve:
        case ParticleValueKindUVE::Gradient:
            break;
    }
    return j;
}

[[nodiscard]] bool TryParseVectorInputUVE(const JsonUVE& j, ParticleVectorInputUVE& out) {
    if (!j.is_object() || j.size() != 1U) {
        return false;
    }
    out = ParticleVectorInputUVE{};
    if (j.contains("constant")) {
        out.kind = ParticleValueKindUVE::Constant;
        return TryParseVec3UVE(j.at("constant"), out.constant);
    }
    if (j.contains("random")) {
        const JsonUVE& r = j.at("random");
        if (!r.is_array() || r.size() != 2U) {
            return false;
        }
        out.kind = ParticleValueKindUVE::RandomRange;
        return TryParseVec3UVE(r[0], out.min) && TryParseVec3UVE(r[1], out.max);
    }
    if (j.contains("parameter")) {
        out.kind = ParticleValueKindUVE::UserParameter;
        out.parameter = j.at("parameter").get<std::string>();
        return true;
    }
    if (j.contains("wire")) {
        out.kind = ParticleValueKindUVE::Wire;
        out.wireNode = j.at("wire").get<std::uint32_t>();
        return true;
    }
    return false;
}

[[nodiscard]] JsonUVE SerializeColorInputUVE(const ParticleColorInputUVE& input) {
    JsonUVE j = JsonUVE::object();
    switch (input.kind) {
        case ParticleValueKindUVE::Constant:
            j["constant"] = JsonUVE::array({input.constant.r, input.constant.g, input.constant.b, input.alpha});
            break;
        case ParticleValueKindUVE::RandomRange:
            j["random"] = JsonUVE::array({
                JsonUVE::array({input.minColor.r, input.minColor.g, input.minColor.b}),
                JsonUVE::array({input.maxColor.r, input.maxColor.g, input.maxColor.b}),
            });
            if (input.alpha != 1.0F) {
                j["alpha"] = input.alpha;
            }
            break;
        case ParticleValueKindUVE::Gradient: {
            JsonUVE stops = JsonUVE::array();
            for (const ParticleGradientStopUVE& s : input.gradient.stops) {
                stops.push_back(JsonUVE::array({s.time, s.color.r, s.color.g, s.color.b, s.alpha}));
            }
            j["gradient"] = std::move(stops);
            break;
        }
        case ParticleValueKindUVE::UserParameter:
            j["parameter"] = input.parameter;
            break;
        case ParticleValueKindUVE::Curve:
        case ParticleValueKindUVE::Wire:
            break;
    }
    return j;
}

[[nodiscard]] bool TryParseColorInputUVE(const JsonUVE& j, ParticleColorInputUVE& out) {
    if (!j.is_object()) {
        return false;
    }
    out = ParticleColorInputUVE{};
    if (j.contains("constant")) {
        if (j.size() != 1U) {
            return false;
        }
        const JsonUVE& c = j.at("constant");
        if (!c.is_array() || (c.size() != 3U && c.size() != 4U)) {
            return false;
        }
        out.kind = ParticleValueKindUVE::Constant;
        out.constant = Math::ColorUVE{c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
        out.alpha = c.size() == 4U ? c[3].get<float>() : 1.0F;
        return true;
    }
    if (j.contains("random")) {
        for (const auto& item : j.items()) {
            if (item.key() != "random" && item.key() != "alpha") {
                return false;
            }
        }
        const JsonUVE& r = j.at("random");
        if (!r.is_array() || r.size() != 2U || !r[0].is_array() || !r[1].is_array() ||
            r[0].size() < 3U || r[0].size() > 4U || r[1].size() < 3U || r[1].size() > 4U) {
            return false;
        }
        out.kind = ParticleValueKindUVE::RandomRange;
        out.minColor = Math::ColorUVE{r[0][0].get<float>(), r[0][1].get<float>(), r[0][2].get<float>()};
        out.maxColor = Math::ColorUVE{r[1][0].get<float>(), r[1][1].get<float>(), r[1][2].get<float>()};
        out.alpha = j.value("alpha", 1.0F);
        return true;
    }
    if (j.contains("gradient")) {
        if (j.size() != 1U || !j.at("gradient").is_array() ||
            j.at("gradient").size() > kMaximumGradientStopsUVE) {
            return false;
        }
        out.kind = ParticleValueKindUVE::Gradient;
        out.gradient.stops.reserve(j.at("gradient").size());
        for (const JsonUVE& s : j.at("gradient")) {
            if (!s.is_array() || s.size() != 5U) {
                return false;
            }
            out.gradient.stops.push_back(ParticleGradientStopUVE{
                s[0].get<float>(),
                Math::ColorUVE{s[1].get<float>(), s[2].get<float>(), s[3].get<float>()},
                s[4].get<float>(),
            });
        }
        return true;
    }
    if (j.contains("parameter")) {
        if (j.size() != 1U) {
            return false;
        }
        out.kind = ParticleValueKindUVE::UserParameter;
        out.parameter = j.at("parameter").get<std::string>();
        return true;
    }
    return false;
}

[[nodiscard]] JsonUVE SerializeModulesUVE(const std::vector<ParticleModuleUVE>& modules) {
    JsonUVE array = JsonUVE::array();
    for (const ParticleModuleUVE& mod : modules) {
        const ParticleModuleDescriptorUVE* desc = FindParticleModuleDescriptorUVE(mod.type);
        if (desc == nullptr) {
            continue;
        }
        JsonUVE entry{{"id", mod.id}, {"type", desc->canonicalName}};
        if (!mod.enabled) {
            entry["enabled"] = false;
        }

        JsonUVE floats = JsonUVE::object();
        for (const auto& [name, val] : mod.floats) {
            const auto defIt = desc->defaultFloats.find(name);
            if (defIt != desc->defaultFloats.end() && defIt->second == val) {
                continue;
            }
            floats[name] = SerializeFloatInputUVE(val);
        }
        if (!floats.empty()) {
            entry["floats"] = std::move(floats);
        }

        JsonUVE vectors = JsonUVE::object();
        for (const auto& [name, val] : mod.vectors) {
            const auto defIt = desc->defaultVectors.find(name);
            if (defIt != desc->defaultVectors.end() && defIt->second == val) {
                continue;
            }
            vectors[name] = SerializeVectorInputUVE(val);
        }
        if (!vectors.empty()) {
            entry["vectors"] = std::move(vectors);
        }

        JsonUVE colors = JsonUVE::object();
        for (const auto& [name, val] : mod.colors) {
            const auto defIt = desc->defaultColors.find(name);
            if (defIt != desc->defaultColors.end() && defIt->second == val) {
                continue;
            }
            colors[name] = SerializeColorInputUVE(val);
        }
        if (!colors.empty()) {
            entry["colors"] = std::move(colors);
        }

        JsonUVE ints = JsonUVE::object();
        for (const auto& [name, val] : mod.ints) {
            const auto defIt = desc->defaultInts.find(name);
            if (defIt != desc->defaultInts.end() && defIt->second == val) {
                continue;
            }
            ints[name] = val;
        }
        if (!ints.empty()) {
            entry["ints"] = std::move(ints);
        }

        JsonUVE strings = JsonUVE::object();
        for (const auto& [name, val] : mod.strings) {
            const auto defIt = desc->defaultStrings.find(name);
            if (defIt != desc->defaultStrings.end() && defIt->second == val) {
                continue;
            }
            strings[name] = val;
        }
        if (!strings.empty()) {
            entry["strings"] = std::move(strings);
        }

        if (mod.graphX != 0.0F || mod.graphY != 0.0F) {
            entry["at"] = JsonUVE::array({mod.graphX, mod.graphY});
        }
        array.push_back(std::move(entry));
    }
    return array;
}

[[nodiscard]] bool TryParseModulesUVE(const JsonUVE& emitterJson, const std::string_view stageKey,
                                      const std::optional<ParticleStageUVE> expectedStage,
                                      std::vector<ParticleModuleUVE>& outModules,
                                      const std::size_t maximumCount = kMaximumParticleModulesPerStageUVE) {
    const std::string keyString(stageKey);
    if (!emitterJson.contains(keyString)) {
        return true;
    }
    const JsonUVE& list = emitterJson.at(keyString);
    if (!list.is_array() || list.size() > maximumCount) {
        return false;
    }
    outModules.reserve(list.size());
    for (const JsonUVE& item : list) {
        if (!item.is_object() || !item.contains("type")) {
            return false;
        }
        const std::string typeName = item.at("type").get<std::string>();
        const ParticleModuleDescriptorUVE* desc = FindParticleModuleDescriptorByNameUVE(typeName);
        if (desc == nullptr || (expectedStage.has_value() && desc->stage != *expectedStage)) {
            return false;
        }
        ParticleModuleUVE mod;
        mod.id = item.value("id", 0U);
        mod.type = desc->type;
        mod.enabled = item.value("enabled", true);
        if (item.contains("at")) {
            const JsonUVE& at = item.at("at");
            if (!at.is_array() || at.size() != 2U) {
                return false;
            }
            mod.graphX = at[0].get<float>();
            mod.graphY = at[1].get<float>();
        }
        if (item.contains("floats")) {
            const JsonUVE& floats = item.at("floats");
            if (!floats.is_object()) {
                return false;
            }
            for (const auto& [k, v] : floats.items()) {
                if (!desc->defaultFloats.contains(k)) {
                    return false;
                }
                ParticleFloatInputUVE parsed;
                if (!TryParseFloatInputUVE(v, parsed)) {
                    return false;
                }
                mod.floats.emplace(k, std::move(parsed));
            }
        }
        if (item.contains("vectors")) {
            const JsonUVE& vectors = item.at("vectors");
            if (!vectors.is_object()) {
                return false;
            }
            for (const auto& [k, v] : vectors.items()) {
                if (!desc->defaultVectors.contains(k)) {
                    return false;
                }
                ParticleVectorInputUVE parsed;
                if (!TryParseVectorInputUVE(v, parsed)) {
                    return false;
                }
                mod.vectors.emplace(k, std::move(parsed));
            }
        }
        if (item.contains("colors")) {
            const JsonUVE& colors = item.at("colors");
            if (!colors.is_object()) {
                return false;
            }
            for (const auto& [k, v] : colors.items()) {
                if (!desc->defaultColors.contains(k)) {
                    return false;
                }
                ParticleColorInputUVE parsed;
                if (!TryParseColorInputUVE(v, parsed)) {
                    return false;
                }
                mod.colors.emplace(k, std::move(parsed));
            }
        }
        if (item.contains("ints")) {
            const JsonUVE& ints = item.at("ints");
            if (!ints.is_object()) {
                return false;
            }
            for (const auto& [k, v] : ints.items()) {
                if (!desc->defaultInts.contains(k) || !v.is_number_integer()) {
                    return false;
                }
                mod.ints.emplace(k, v.get<std::int32_t>());
            }
        }
        if (item.contains("strings")) {
            const JsonUVE& strings = item.at("strings");
            if (!strings.is_object()) {
                return false;
            }
            for (const auto& [k, v] : strings.items()) {
                if (!desc->defaultStrings.contains(k) || !v.is_string()) {
                    return false;
                }
                mod.strings.emplace(k, v.get<std::string>());
            }
        }
        outModules.push_back(std::move(mod));
    }
    return true;
}

} // namespace

const std::vector<ParticleModuleDescriptorUVE>& GetParticleModuleCatalogUVE() noexcept {
    static const std::vector<ParticleModuleDescriptorUVE> catalog = BuildModuleCatalogUVE();
    return catalog;
}

const ParticleModuleDescriptorUVE* FindParticleModuleDescriptorUVE(const ParticleModuleTypeUVE type) noexcept {
    const auto& catalog = GetParticleModuleCatalogUVE();
    for (const ParticleModuleDescriptorUVE& entry : catalog) {
        if (entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

const ParticleModuleDescriptorUVE* FindParticleModuleDescriptorByNameUVE(
    const std::string_view canonicalName) noexcept {
    const auto& catalog = GetParticleModuleCatalogUVE();
    for (const ParticleModuleDescriptorUVE& entry : catalog) {
        if (entry.canonicalName == canonicalName) {
            return &entry;
        }
    }
    return nullptr;
}

ParticleModuleUVE MakeDefaultParticleModuleUVE(const std::uint32_t id, const ParticleModuleTypeUVE type,
                                               const float graphX, const float graphY) {
    ParticleModuleUVE mod;
    mod.id = id;
    mod.type = type;
    mod.enabled = true;
    mod.graphX = graphX;
    mod.graphY = graphY;
    return mod;
}

bool IsParticleSystemAssetValidUVE(const ParticleSystemAssetUVE& system) noexcept {
    if (system.emitters.empty() || system.emitters.size() > kMaximumParticleEmittersUVE ||
        system.parameters.size() > kMaximumParticleUserParametersUVE) {
        return false;
    }
    if (!std::isfinite(system.duration) || system.duration <= 0.0F ||
        !std::isfinite(system.warmUpSeconds) || system.warmUpSeconds < 0.0F) {
        return false;
    }
    if (!Math::IsFiniteUVE(system.fixedBounds.min) || !Math::IsFiniteUVE(system.fixedBounds.max) ||
        system.fixedBounds.min.x > system.fixedBounds.max.x ||
        system.fixedBounds.min.y > system.fixedBounds.max.y ||
        system.fixedBounds.min.z > system.fixedBounds.max.z) {
        return false;
    }
    for (const ParticleScalabilityUVE& tier : system.scalability) {
        if (!std::isfinite(tier.spawnScale) || tier.spawnScale < 0.0F ||
            !std::isfinite(tier.cullDistance) || tier.cullDistance < 0.0F) {
            return false;
        }
    }

    std::set<std::string> paramNames;
    for (const ParticleUserParameterUVE& param : system.parameters) {
        if (!NameOkUVE(param.name) ||
            static_cast<std::uint8_t>(param.type) > static_cast<std::uint8_t>(ParticleUserParameterTypeUVE::Int) ||
            !Math::IsFiniteUVE(param.value) || !std::isfinite(param.alpha) ||
            !paramNames.insert(param.name).second) {
            return false;
        }
    }

    std::set<std::string> emitterNames;
    for (const ParticleEmitterAssetUVE& emitter : system.emitters) {
        if (!NameOkUVE(emitter.name) || !emitterNames.insert(emitter.name).second) {
            return false;
        }
        if (static_cast<std::uint8_t>(emitter.space) > static_cast<std::uint8_t>(ParticleSimSpaceUVE::Local) ||
            static_cast<std::uint8_t>(emitter.target) > static_cast<std::uint8_t>(ParticleSimTargetUVE::Auto)) {
            return false;
        }
        if (emitter.maxParticles == 0U || emitter.maxParticles > kMaximumParticlesPerEmitterUVE) {
            return false;
        }
        if (!ValidateStageModulesUVE(emitter.emitterUpdate, ParticleStageUVE::EmitterUpdate, system.parameters) ||
            !ValidateStageModulesUVE(emitter.particleSpawn, ParticleStageUVE::ParticleSpawn, system.parameters) ||
            !ValidateStageModulesUVE(emitter.particleUpdate, ParticleStageUVE::ParticleUpdate, system.parameters) ||
            !ValidateStageModulesUVE(emitter.render, ParticleStageUVE::Render, system.parameters) ||
            !ValidateStageModulesUVE(emitter.detached, std::nullopt, system.parameters,
                                     kMaximumParticleDetachedModulesUVE)) {
            return false;
        }
        if (emitter.mathNodes.size() > kMaximumParticleMathNodesUVE ||
            emitter.comments.size() > kMaximumParticleCommentsUVE) {
            return false;
        }
        for (const ParticleMathNodeUVE& math : emitter.mathNodes) {
            if (!NameOkUVE(math.op) || !std::isfinite(math.constant) ||
                !std::isfinite(math.graphX) || !std::isfinite(math.graphY) ||
                math.inputs.size() > 16U) {
                return false;
            }
        }
    }
    return true;
}

bool SaveParticleSystemAssetUVE(const ParticleSystemAssetUVE& system, const std::filesystem::path& path) {
    if (!IsParticleSystemAssetValidUVE(system)) {
        UVE_ERROR("ParticleSystemAssetUVE: refused to save invalid particle system to {}", path.string());
        return false;
    }

    JsonUVE document;
    document["schema"] = kParticleSystemSchemaUVE;
    document["duration"] = system.duration;
    document["looping"] = system.looping;
    document["warmUp"] = system.warmUpSeconds;
    document["bounds"] = {
        {"min", SerializeVec3UVE(system.fixedBounds.min)},
        {"max", SerializeVec3UVE(system.fixedBounds.max)},
    };

    if (!system.parameters.empty()) {
        JsonUVE params = JsonUVE::array();
        for (const ParticleUserParameterUVE& param : system.parameters) {
            JsonUVE p{
                {"name", param.name},
                {"type", ParameterTypeToJsonUVE(param.type)},
                {"value", SerializeVec3UVE(param.value)},
            };
            if (param.type == ParticleUserParameterTypeUVE::Color || param.alpha != 1.0F) {
                p["alpha"] = param.alpha;
            }
            params.push_back(std::move(p));
        }
        document["parameters"] = std::move(params);
    }

    const ParticleScalabilityUVE defaultTier{};
    bool anyScalabilityNonDefault = false;
    JsonUVE scalability = JsonUVE::array();
    for (const ParticleScalabilityUVE& tier : system.scalability) {
        JsonUVE t = JsonUVE::object();
        if (tier.spawnScale != defaultTier.spawnScale) {
            t["spawnScale"] = tier.spawnScale;
            anyScalabilityNonDefault = true;
        }
        if (tier.maxParticlesCap != defaultTier.maxParticlesCap) {
            t["maxParticles"] = tier.maxParticlesCap;
            anyScalabilityNonDefault = true;
        }
        if (tier.cullDistance != defaultTier.cullDistance) {
            t["cullDistance"] = tier.cullDistance;
            anyScalabilityNonDefault = true;
        }
        scalability.push_back(std::move(t));
    }
    if (anyScalabilityNonDefault) {
        document["scalability"] = std::move(scalability);
    }

    JsonUVE emitters = JsonUVE::array();
    for (const ParticleEmitterAssetUVE& emitter : system.emitters) {
        JsonUVE e{
            {"name", emitter.name},
            {"space", SpaceToJsonUVE(emitter.space)},
            {"target", TargetToJsonUVE(emitter.target)},
            {"maxParticles", emitter.maxParticles},
        };
        if (!emitter.enabled) {
            e["enabled"] = false;
        }
        if (emitter.seed != 0U) {
            e["seed"] = emitter.seed;
        }
        if (!emitter.emitterUpdate.empty()) {
            e["emitterUpdate"] = SerializeModulesUVE(emitter.emitterUpdate);
        }
        if (!emitter.particleSpawn.empty()) {
            e["particleSpawn"] = SerializeModulesUVE(emitter.particleSpawn);
        }
        if (!emitter.particleUpdate.empty()) {
            e["particleUpdate"] = SerializeModulesUVE(emitter.particleUpdate);
        }
        if (!emitter.render.empty()) {
            e["render"] = SerializeModulesUVE(emitter.render);
        }
        if (!emitter.detached.empty()) {
            e["detached"] = SerializeModulesUVE(emitter.detached);
        }
        if (!emitter.mathNodes.empty()) {
            JsonUVE nodes = JsonUVE::array();
            for (const ParticleMathNodeUVE& math : emitter.mathNodes) {
                JsonUVE n{
                    {"id", math.id},
                    {"op", math.op},
                };
                if (!math.inputs.empty()) {
                    n["inputs"] = math.inputs;
                }
                if (math.constant != 0.0F) {
                    n["constant"] = math.constant;
                }
                if (math.graphX != 0.0F || math.graphY != 0.0F) {
                    n["at"] = JsonUVE::array({math.graphX, math.graphY});
                }
                nodes.push_back(std::move(n));
            }
            e["mathNodes"] = std::move(nodes);
        }
        if (!emitter.comments.empty()) {
            JsonUVE comments = JsonUVE::array();
            for (const auto& [a, b] : emitter.comments) {
                comments.push_back(JsonUVE::array({a, b}));
            }
            e["comments"] = std::move(comments);
        }
        emitters.push_back(std::move(e));
    }
    document["emitters"] = std::move(emitters);

    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumParticleSystemPayloadBytesUVE) {
        UVE_ERROR("ParticleSystemAssetUVE: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFileUVE(path, AssetKindUVE::ParticleSystem,
                           std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadParticleSystemAssetUVE(const std::filesystem::path& path, ParticleSystemAssetUVE& outSystem) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::ParticleSystem || file->second.empty() ||
        file->second.size() > kMaximumParticleSystemPayloadBytesUVE) {
        UVE_ERROR("ParticleSystemAssetUVE: invalid particle system envelope {}", path.string());
        return false;
    }

    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const JsonUVE document = JsonUVE::parse(serialized);
        if (!document.is_object() || document.value("schema", std::string{}) != kParticleSystemSchemaUVE ||
            !document.contains("emitters") || !document.at("emitters").is_array()) {
            UVE_ERROR("ParticleSystemAssetUVE: invalid schema or emitters in {}", path.string());
            return false;
        }

        ParticleSystemAssetUVE candidate;
        candidate.duration = document.value("duration", candidate.duration);
        candidate.looping = document.value("looping", candidate.looping);
        candidate.warmUpSeconds = document.value("warmUp", candidate.warmUpSeconds);
        if (document.contains("bounds")) {
            const JsonUVE& bounds = document.at("bounds");
            if (!bounds.is_object() || !bounds.contains("min") || !bounds.contains("max") ||
                !TryParseVec3UVE(bounds.at("min"), candidate.fixedBounds.min) ||
                !TryParseVec3UVE(bounds.at("max"), candidate.fixedBounds.max)) {
                return false;
            }
        }

        if (document.contains("parameters")) {
            const JsonUVE& params = document.at("parameters");
            if (!params.is_array() || params.size() > kMaximumParticleUserParametersUVE) {
                return false;
            }
            candidate.parameters.reserve(params.size());
            for (const JsonUVE& p : params) {
                if (!p.is_object() || !p.contains("name") || !p.contains("type")) {
                    return false;
                }
                ParticleUserParameterUVE param;
                param.name = p.at("name").get<std::string>();
                if (!TryParseParameterTypeUVE(p.at("type").get<std::string>(), param.type)) {
                    return false;
                }
                if (p.contains("value")) {
                    const JsonUVE& val = p.at("value");
                    if (val.is_array()) {
                        if (!TryParseVec3UVE(val, param.value)) {
                            return false;
                        }
                    } else if (val.is_number()) {
                        param.value = Math::Vector3UVE{val.get<float>(), 0.0F, 0.0F};
                    } else if (val.is_boolean()) {
                        param.value = Math::Vector3UVE{val.get<bool>() ? 1.0F : 0.0F, 0.0F, 0.0F};
                    } else {
                        return false;
                    }
                }
                param.alpha = p.value("alpha", 1.0F);
                candidate.parameters.push_back(std::move(param));
            }
        }

        if (document.contains("scalability")) {
            const JsonUVE& tiers = document.at("scalability");
            if (!tiers.is_array() || tiers.size() > candidate.scalability.size()) {
                return false;
            }
            for (std::size_t idx = 0U; idx < tiers.size(); ++idx) {
                const JsonUVE& t = tiers[idx];
                if (!t.is_object()) {
                    return false;
                }
                candidate.scalability[idx].spawnScale = t.value("spawnScale", 1.0F);
                candidate.scalability[idx].maxParticlesCap = t.value("maxParticles", 0U);
                candidate.scalability[idx].cullDistance = t.value("cullDistance", 0.0F);
            }
        }

        const JsonUVE& emitters = document.at("emitters");
        if (emitters.empty() || emitters.size() > kMaximumParticleEmittersUVE) {
            return false;
        }
        candidate.emitters.reserve(emitters.size());
        for (const JsonUVE& e : emitters) {
            if (!e.is_object() || !e.contains("name")) {
                return false;
            }
            ParticleEmitterAssetUVE emitter;
            emitter.name = e.at("name").get<std::string>();
            emitter.enabled = e.value("enabled", true);
            if (e.contains("space") && !TryParseSpaceUVE(e.at("space").get<std::string>(), emitter.space)) {
                return false;
            }
            if (e.contains("target") && !TryParseTargetUVE(e.at("target").get<std::string>(), emitter.target)) {
                return false;
            }
            emitter.maxParticles = e.value("maxParticles", emitter.maxParticles);
            emitter.seed = e.value("seed", 0U);
            if (!TryParseModulesUVE(e, "emitterUpdate", ParticleStageUVE::EmitterUpdate, emitter.emitterUpdate) ||
                !TryParseModulesUVE(e, "particleSpawn", ParticleStageUVE::ParticleSpawn, emitter.particleSpawn) ||
                !TryParseModulesUVE(e, "particleUpdate", ParticleStageUVE::ParticleUpdate, emitter.particleUpdate) ||
                !TryParseModulesUVE(e, "render", ParticleStageUVE::Render, emitter.render) ||
                !TryParseModulesUVE(e, "detached", std::nullopt, emitter.detached,
                                    kMaximumParticleDetachedModulesUVE)) {
                return false;
            }
            if (e.contains("mathNodes")) {
                const JsonUVE& nodes = e.at("mathNodes");
                if (!nodes.is_array() || nodes.size() > kMaximumParticleMathNodesUVE) {
                    return false;
                }
                for (const JsonUVE& n : nodes) {
                    if (!n.is_object() || !n.contains("op")) {
                        return false;
                    }
                    ParticleMathNodeUVE math;
                    math.id = n.value("id", 0U);
                    math.op = n.at("op").get<std::string>();
                    math.inputs = n.value("inputs", std::vector<std::uint32_t>{});
                    math.constant = n.value("constant", 0.0F);
                    if (n.contains("at")) {
                        const JsonUVE& at = n.at("at");
                        if (!at.is_array() || at.size() != 2U) {
                            return false;
                        }
                        math.graphX = at[0].get<float>();
                        math.graphY = at[1].get<float>();
                    }
                    emitter.mathNodes.push_back(std::move(math));
                }
            }
            if (e.contains("comments")) {
                const JsonUVE& comments = e.at("comments");
                if (!comments.is_array() || comments.size() > kMaximumParticleCommentsUVE) {
                    return false;
                }
                for (const JsonUVE& c : comments) {
                    if (!c.is_array() || c.size() != 2U) {
                        return false;
                    }
                    emitter.comments.emplace_back(c[0].get<std::uint32_t>(), c[1].get<std::uint32_t>());
                }
            }
            candidate.emitters.push_back(std::move(emitter));
        }

        if (!IsParticleSystemAssetValidUVE(candidate)) {
            UVE_ERROR("ParticleSystemAssetUVE: loaded system failed validation in {}", path.string());
            return false;
        }
        outSystem = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("ParticleSystemAssetUVE: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("ParticleSystemAssetUVE: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
