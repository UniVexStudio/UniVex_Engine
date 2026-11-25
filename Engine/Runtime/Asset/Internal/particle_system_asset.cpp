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

using Json = nlohmann::json;

constexpr std::string_view kParticleSystemSchema = "uve-particle-system-v1";
constexpr std::size_t kMaximumParticleMathNodes = 256U;
constexpr std::size_t kMaximumParticleComments = 256U;

[[nodiscard]] ParticleFloatInput MakeFloat(const float value) noexcept {
    ParticleFloatInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleFloatInput MakeUnitCurveFloat() {
    ParticleFloatInput input;
    input.kind = ParticleValueKind::Curve;
    input.scale = 1.0F;
    input.curve.smooth = true;
    input.curve.keys = {{0.0F, 1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F, 0.0F}};
    return input;
}

[[nodiscard]] ParticleVectorInput MakeVector(const Math::Vector3& value) noexcept {
    ParticleVectorInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = value;
    return input;
}

[[nodiscard]] ParticleColorInput MakeColor(const Math::Color& value, const float alpha = 1.0F) noexcept {
    ParticleColorInput input;
    input.kind = ParticleValueKind::Constant;
    input.constant = value;
    input.alpha = alpha;
    return input;
}

[[nodiscard]] ParticleColorInput MakeWhiteGradientColor() {
    ParticleColorInput input;
    input.kind = ParticleValueKind::Gradient;
    input.gradient.stops = {
        {0.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
        {1.0F, {1.0F, 1.0F, 1.0F}, 1.0F},
    };
    return input;
}

[[nodiscard]] std::vector<ParticleModuleDescriptor> BuildModuleCatalog() {
    std::vector<ParticleModuleDescriptor> catalog;
    catalog.reserve(46U);

    // ---- Emitter Update ----
    catalog.push_back({
        .type = ParticleModuleType::SpawnRate,
        .stage = ParticleStage::EmitterUpdate,
        .canonicalName = "SpawnRate",
        .displayName = "Spawn Rate",
        .description = "Emits particles continuously at a rate per second.",
        .defaultFloats = {{"Rate", MakeFloat(10.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::SpawnBurst,
        .stage = ParticleStage::EmitterUpdate,
        .canonicalName = "SpawnBurst",
        .displayName = "Spawn Burst",
        .description = "Emits bursts of particles at scheduled times in each loop.",
        .defaultFloats = {{"Time", MakeFloat(0.0F)}, {"Interval", MakeFloat(0.0F)}},
        .defaultInts = {{"Count", 10}, {"Cycles", 1}},
    });
    catalog.push_back({
        .type = ParticleModuleType::SpawnPerDistance,
        .stage = ParticleStage::EmitterUpdate,
        .canonicalName = "SpawnPerDistance",
        .displayName = "Spawn Per Distance",
        .description = "Emits particles proportionally to emitter movement distance.",
        .defaultFloats = {{"PerUnit", MakeFloat(1.0F)}},
    });

    // ---- Particle Spawn ----
    catalog.push_back({
        .type = ParticleModuleType::ShapePoint,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "ShapePoint",
        .displayName = "Shape: Point",
        .description = "Spawns particles at the emitter origin.",
        .defaultFloats = {{"NormalSpeed", MakeFloat(0.0F)}},
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::ShapeSphere,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "ShapeSphere",
        .displayName = "Shape: Sphere",
        .description = "Spawns particles inside or on the surface of a sphere or hemisphere.",
        .defaultFloats = {{"Radius", MakeFloat(1.0F)}, {"NormalSpeed", MakeFloat(0.0F)}},
        .defaultInts = {{"Surface", 0}, {"Hemisphere", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::ShapeBox,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "ShapeBox",
        .displayName = "Shape: Box",
        .description = "Spawns particles inside or on the surface of a box.",
        .defaultFloats = {{"NormalSpeed", MakeFloat(0.0F)}},
        .defaultVectors = {{"Size", MakeVector({1.0F, 1.0F, 1.0F})}},
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::ShapeCone,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "ShapeCone",
        .displayName = "Shape: Cone",
        .description = "Spawns particles inside or on the surface of a cone.",
        .defaultFloats = {
            {"Angle", MakeFloat(25.0F)},
            {"Length", MakeFloat(1.0F)},
            {"Radius", MakeFloat(0.2F)},
            {"NormalSpeed", MakeFloat(0.0F)},
        },
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::ShapeCylinder,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "ShapeCylinder",
        .displayName = "Shape: Cylinder",
        .description = "Spawns particles inside or on the surface of a cylinder.",
        .defaultFloats = {
            {"Radius", MakeFloat(0.5F)},
            {"Height", MakeFloat(1.0F)},
            {"NormalSpeed", MakeFloat(0.0F)},
        },
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::ShapeTorus,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "ShapeTorus",
        .displayName = "Shape: Torus",
        .description = "Spawns particles inside or on the surface of a torus.",
        .defaultFloats = {
            {"MajorRadius", MakeFloat(1.0F)},
            {"MinorRadius", MakeFloat(0.25F)},
            {"NormalSpeed", MakeFloat(0.0F)},
        },
        .defaultInts = {{"Surface", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::ShapeMeshSurface,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "ShapeMeshSurface",
        .displayName = "Shape: Mesh Surface",
        .description = "Spawns particles uniformly across the triangles of a mesh.",
        .defaultFloats = {{"NormalSpeed", MakeFloat(0.0F)}},
        .defaultInts = {{"Surface", 1}},
        .defaultStrings = {{"Mesh", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialVelocity,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialVelocity",
        .displayName = "Initial Velocity",
        .description = "Sets each particle's starting velocity vector.",
        .defaultVectors = {{"Velocity", MakeVector({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::VelocityInCone,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "VelocityInCone",
        .displayName = "Velocity In Cone",
        .description = "Launches particles within a cone around an axis.",
        .defaultFloats = {{"Angle", MakeFloat(25.0F)}, {"Speed", MakeFloat(1.0F)}},
        .defaultVectors = {{"Axis", MakeVector({0.0F, 1.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialLifetime,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialLifetime",
        .displayName = "Initial Lifetime",
        .description = "Sets how many seconds each particle stays alive.",
        .defaultFloats = {{"Lifetime", MakeFloat(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialColor,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialColor",
        .displayName = "Initial Color",
        .description = "Sets each particle's initial linear color and opacity.",
        .defaultColors = {{"Color", MakeColor({1.0F, 1.0F, 1.0F}, 1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialSize,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialSize",
        .displayName = "Initial Size",
        .description = "Sets each particle's starting uniform or 2D size.",
        .defaultFloats = {{"Size", MakeFloat(1.0F)}},
        .defaultVectors = {{"Size", MakeVector({1.0F, 1.0F, 1.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialRotation,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialRotation",
        .displayName = "Initial Rotation",
        .description = "Sets each particle's starting angle in degrees.",
        .defaultFloats = {{"Rotation", MakeFloat(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialSpin,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialSpin",
        .displayName = "Initial Spin",
        .description = "Sets each particle's starting angular velocity in degrees per second.",
        .defaultFloats = {{"Spin", MakeFloat(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialSpriteFrame,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialSpriteFrame",
        .displayName = "Initial Sprite Frame",
        .description = "Sets each particle's starting sprite flipbook frame index.",
        .defaultFloats = {{"Frame", MakeFloat(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InitialMass,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InitialMass",
        .displayName = "Initial Mass",
        .description = "Sets each particle's physical mass.",
        .defaultFloats = {{"Mass", MakeFloat(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::InheritOwnerVelocity,
        .stage = ParticleStage::ParticleSpawn,
        .canonicalName = "InheritOwnerVelocity",
        .displayName = "Inherit Owner Velocity",
        .description = "Adds a scaled fraction of the emitter owner's velocity on spawn.",
        .defaultFloats = {{"Scale", MakeFloat(1.0F)}},
    });

    // ---- Particle Update ----
    catalog.push_back({
        .type = ParticleModuleType::Gravity,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "Gravity",
        .displayName = "Gravity",
        .description = "Applies constant acceleration such as gravity each frame.",
        .defaultVectors = {{"Acceleration", MakeVector({0.0F, -9.8F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::Drag,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "Drag",
        .displayName = "Drag",
        .description = "Slows particles proportionally to their current velocity.",
        .defaultFloats = {{"Coefficient", MakeFloat(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::AddVelocity,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "AddVelocity",
        .displayName = "Add Velocity",
        .description = "Applies a steady directional push per second to live velocities.",
        .defaultVectors = {{"Velocity", MakeVector({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::CurlNoise,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "CurlNoise",
        .displayName = "Curl Noise",
        .description = "Adds divergence-free turbulent acceleration from 3D curl noise.",
        .defaultFloats = {
            {"Strength", MakeFloat(1.0F)},
            {"Frequency", MakeFloat(1.0F)},
            {"Speed", MakeFloat(0.5F)},
        },
    });
    catalog.push_back({
        .type = ParticleModuleType::Vortex,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "Vortex",
        .displayName = "Vortex",
        .description = "Swirls particles tangentially around an axis and center point.",
        .defaultFloats = {{"Strength", MakeFloat(2.0F)}, {"PullIn", MakeFloat(0.0F)}},
        .defaultVectors = {
            {"Axis", MakeVector({0.0F, 1.0F, 0.0F})},
            {"Center", MakeVector({0.0F, 0.0F, 0.0F})},
        },
    });
    catalog.push_back({
        .type = ParticleModuleType::PointAttractor,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "PointAttractor",
        .displayName = "Point Attractor",
        .description = "Pulls particles toward a point within a radius and optionally removes close hits.",
        .defaultFloats = {
            {"Strength", MakeFloat(5.0F)},
            {"Radius", MakeFloat(5.0F)},
            {"KillRadius", MakeFloat(0.0F)},
        },
        .defaultVectors = {{"Position", MakeVector({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::Wind,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "Wind",
        .displayName = "Wind",
        .description = "Steers particle velocities toward a wind direction with turbulence.",
        .defaultFloats = {{"Turbulence", MakeFloat(0.0F)}},
        .defaultVectors = {{"Velocity", MakeVector({0.0F, 0.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::LimitSpeed,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "LimitSpeed",
        .displayName = "Limit Speed",
        .description = "Damps particle speed when velocity exceeds the maximum threshold.",
        .defaultFloats = {{"MaxSpeed", MakeFloat(10.0F)}, {"Damping", MakeFloat(1.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::Orbit,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "Orbit",
        .displayName = "Orbit",
        .description = "Moves particles along a circular orbit around their spawn point.",
        .defaultFloats = {{"Radius", MakeFloat(1.0F)}, {"Speed", MakeFloat(1.0F)}},
        .defaultVectors = {{"Axis", MakeVector({0.0F, 1.0F, 0.0F})}},
    });
    catalog.push_back({
        .type = ParticleModuleType::ColorOverLife,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "ColorOverLife",
        .displayName = "Color Over Life",
        .description = "Multiplies particle color over normalized lifetime with a gradient.",
        .defaultColors = {{"Color", MakeWhiteGradientColor()}},
    });
    catalog.push_back({
        .type = ParticleModuleType::SizeOverLife,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "SizeOverLife",
        .displayName = "Size Over Life",
        .description = "Scales particle size over normalized lifetime with a curve.",
        .defaultFloats = {{"Scale", MakeUnitCurveFloat()}},
    });
    catalog.push_back({
        .type = ParticleModuleType::RotationRate,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "RotationRate",
        .displayName = "Rotation Rate",
        .description = "Drives particle angular spin in degrees per second.",
        .defaultFloats = {{"Rate", MakeFloat(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::SizeBySpeed,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "SizeBySpeed",
        .displayName = "Size By Speed",
        .description = "Remaps particle speed smoothly to a size scale range.",
        .defaultFloats = {
            {"MinSpeed", MakeFloat(0.0F)},
            {"MaxSpeed", MakeFloat(10.0F)},
            {"MinScale", MakeFloat(1.0F)},
            {"MaxScale", MakeFloat(1.0F)},
        },
    });
    catalog.push_back({
        .type = ParticleModuleType::SpriteFlipbook,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "SpriteFlipbook",
        .displayName = "Sprite Flipbook",
        .description = "Advances sprite sheet frame index over age or at a fixed frame rate.",
        .defaultFloats = {{"Fps", MakeFloat(12.0F)}},
        .defaultInts = {{"Rows", 1}, {"Columns", 1}, {"Mode", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::CollisionPlane,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "CollisionPlane",
        .displayName = "Collision: Plane",
        .description = "Bounces particles against an infinite plane with friction and optional removal.",
        .defaultFloats = {
            {"Bounce", MakeFloat(0.4F)},
            {"Friction", MakeFloat(0.1F)},
            {"LifeLoss", MakeFloat(0.0F)},
        },
        .defaultVectors = {
            {"Point", MakeVector({0.0F, 0.0F, 0.0F})},
            {"Normal", MakeVector({0.0F, 1.0F, 0.0F})},
        },
        .defaultInts = {{"KillOnHit", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::CollisionDepth,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "CollisionDepth",
        .displayName = "Collision: Depth",
        .description = "Collides GPU particles against the scene depth buffer.",
        .defaultFloats = {
            {"Bounce", MakeFloat(0.4F)},
            {"Friction", MakeFloat(0.1F)},
            {"LifeLoss", MakeFloat(0.0F)},
        },
        .defaultVectors = {
            {"Point", MakeVector({0.0F, 0.0F, 0.0F})},
            {"Normal", MakeVector({0.0F, 1.0F, 0.0F})},
        },
        .defaultInts = {{"KillOnHit", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::CollisionPhysics,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "CollisionPhysics",
        .displayName = "Collision: Physics",
        .description = "Traces CPU particle motion rays against scene collision shapes.",
        .defaultFloats = {
            {"Bounce", MakeFloat(0.4F)},
            {"Friction", MakeFloat(0.1F)},
            {"LifeLoss", MakeFloat(0.0F)},
        },
        .defaultVectors = {
            {"Point", MakeVector({0.0F, 0.0F, 0.0F})},
            {"Normal", MakeVector({0.0F, 1.0F, 0.0F})},
        },
        .defaultInts = {{"KillOnHit", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::KillByAge,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "KillByAge",
        .displayName = "Kill By Age",
        .description = "Removes particles once their age reaches their lifetime.",
    });
    catalog.push_back({
        .type = ParticleModuleType::KillByBounds,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "KillByBounds",
        .displayName = "Kill By Bounds",
        .description = "Removes particles that leave or enter an axis-aligned box.",
        .defaultVectors = {
            {"Min", MakeVector({-10.0F, -10.0F, -10.0F})},
            {"Max", MakeVector({10.0F, 10.0F, 10.0F})},
        },
        .defaultInts = {{"Inside", 0}},
    });
    catalog.push_back({
        .type = ParticleModuleType::KillByCondition,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "KillByCondition",
        .displayName = "Kill By Condition",
        .description = "Removes particles whenever the wired condition evaluates above 0.5.",
        .defaultFloats = {{"Condition", MakeFloat(0.0F)}},
    });
    catalog.push_back({
        .type = ParticleModuleType::EventOnDeath,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "EventOnDeath",
        .displayName = "Event On Death",
        .description = "Emits spawn events into another emitter when particles die.",
        .defaultInts = {{"Count", 1}},
        .defaultStrings = {{"TargetEmitter", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleType::EventOnCollision,
        .stage = ParticleStage::ParticleUpdate,
        .canonicalName = "EventOnCollision",
        .displayName = "Event On Collision",
        .description = "Emits spawn events into another emitter when particles collide.",
        .defaultInts = {{"Count", 1}},
        .defaultStrings = {{"TargetEmitter", ""}},
    });

    // ---- Render ----
    catalog.push_back({
        .type = ParticleModuleType::SpriteRenderer,
        .stage = ParticleStage::Render,
        .canonicalName = "SpriteRenderer",
        .displayName = "Sprite Renderer",
        .description = "Draws instanced billboard or aligned quads for each particle.",
        .defaultFloats = {
            {"StretchScale", MakeFloat(1.0F)},
            {"AlphaCutoff", MakeFloat(0.5F)},
            {"SoftDistance", MakeFloat(0.0F)},
            {"CameraFadeNear", MakeFloat(0.0F)},
            {"CameraFadeFar", MakeFloat(0.0F)},
            {"Emissive", MakeFloat(1.0F)},
        },
        .defaultVectors = {{"Axis", MakeVector({0.0F, 1.0F, 0.0F})}},
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
        .type = ParticleModuleType::RibbonRenderer,
        .stage = ParticleStage::Render,
        .canonicalName = "RibbonRenderer",
        .displayName = "Ribbon Renderer",
        .description = "Draws camera-facing ribbon trails connecting particles or position histories.",
        .defaultFloats = {
            {"Width", MakeFloat(0.1F)},
            {"TileLength", MakeFloat(1.0F)},
            {"Emissive", MakeFloat(1.0F)},
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
        .type = ParticleModuleType::MeshRenderer,
        .stage = ParticleStage::Render,
        .canonicalName = "MeshRenderer",
        .displayName = "Mesh Renderer",
        .description = "Draws an instanced 3D mesh per live particle.",
        .defaultVectors = {{"Axis", MakeVector({0.0F, 1.0F, 0.0F})}},
        .defaultInts = {{"Orient", 0}},
        .defaultStrings = {{"Mesh", ""}, {"Material", ""}},
    });
    catalog.push_back({
        .type = ParticleModuleType::LightRenderer,
        .stage = ParticleStage::Render,
        .canonicalName = "LightRenderer",
        .displayName = "Light Renderer",
        .description = "Emits dynamic point lights from particles within the renderer budget.",
        .defaultFloats = {{"Radius", MakeFloat(2.0F)}, {"Intensity", MakeFloat(1.0F)}},
    });

    return catalog;
}

[[nodiscard]] bool NameOk(const std::string_view name) noexcept {
    return !name.empty() && name.size() <= kMaximumParticleNameBytes && !name.contains('\0');
}

[[nodiscard]] bool ColorFinite(const Math::Color& color) noexcept {
    return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b);
}

[[nodiscard]] bool CurveOk(const ParticleCurve& curve) noexcept {
    if (curve.keys.size() > kMaximumCurveKeys) {
        return false;
    }
    float previousTime = -1.0F;
    for (std::size_t index = 0U; index < curve.keys.size(); ++index) {
        const ParticleCurveKey& key = curve.keys[index];
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

[[nodiscard]] bool GradientOk(const ParticleGradient& gradient) noexcept {
    if (gradient.stops.size() > kMaximumGradientStops) {
        return false;
    }
    float previousTime = -1.0F;
    for (std::size_t index = 0U; index < gradient.stops.size(); ++index) {
        const ParticleGradientStop& stop = gradient.stops[index];
        if (!std::isfinite(stop.time) || !ColorFinite(stop.color) || !std::isfinite(stop.alpha) ||
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

[[nodiscard]] const ParticleUserParameter* FindParameter(
    const std::vector<ParticleUserParameter>& parameters,
    const std::string_view name) noexcept {
    for (const ParticleUserParameter& param : parameters) {
        if (param.name == name) {
            return &param;
        }
    }
    return nullptr;
}

[[nodiscard]] bool ValidateFloatInput(const ParticleFloatInput& input,
                                         const std::vector<ParticleUserParameter>& parameters) noexcept {
    if (input.kind != ParticleValueKind::Constant && input.kind != ParticleValueKind::RandomRange &&
        input.kind != ParticleValueKind::Curve && input.kind != ParticleValueKind::UserParameter &&
        input.kind != ParticleValueKind::Wire) {
        return false;
    }
    if (!std::isfinite(input.constant) || !std::isfinite(input.min) || !std::isfinite(input.max) ||
        !std::isfinite(input.scale) || input.parameter.contains('\0')) {
        return false;
    }
    if (input.kind == ParticleValueKind::RandomRange && input.min > input.max) {
        return false;
    }
    if (!CurveOk(input.curve)) {
        return false;
    }
    if (input.kind == ParticleValueKind::UserParameter) {
        const ParticleUserParameter* param = FindParameter(parameters, input.parameter);
        if (param == nullptr || param->type != ParticleUserParameterType::Float) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateVectorInput(const ParticleVectorInput& input,
                                          const std::vector<ParticleUserParameter>& parameters) noexcept {
    if (input.kind != ParticleValueKind::Constant && input.kind != ParticleValueKind::RandomRange &&
        input.kind != ParticleValueKind::UserParameter && input.kind != ParticleValueKind::Wire) {
        return false;
    }
    if (!Math::IsFinite(input.constant) || !Math::IsFinite(input.min) || !Math::IsFinite(input.max) ||
        input.parameter.contains('\0')) {
        return false;
    }
    if (input.kind == ParticleValueKind::RandomRange &&
        (input.min.x > input.max.x || input.min.y > input.max.y || input.min.z > input.max.z)) {
        return false;
    }
    if (input.kind == ParticleValueKind::UserParameter) {
        const ParticleUserParameter* param = FindParameter(parameters, input.parameter);
        if (param == nullptr || param->type != ParticleUserParameterType::Vector) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateColorInput(const ParticleColorInput& input,
                                         const std::vector<ParticleUserParameter>& parameters) noexcept {
    if (input.kind != ParticleValueKind::Constant && input.kind != ParticleValueKind::RandomRange &&
        input.kind != ParticleValueKind::Gradient && input.kind != ParticleValueKind::UserParameter) {
        return false;
    }
    if (!ColorFinite(input.constant) || !std::isfinite(input.alpha) ||
        !ColorFinite(input.minColor) || !ColorFinite(input.maxColor) ||
        input.parameter.contains('\0')) {
        return false;
    }
    if (!GradientOk(input.gradient)) {
        return false;
    }
    if (input.kind == ParticleValueKind::UserParameter) {
        const ParticleUserParameter* param = FindParameter(parameters, input.parameter);
        if (param == nullptr || param->type != ParticleUserParameterType::Color) {
            return false;
        }
    }
    return true;
}

/// `expectedStage` empty: any stage is accepted (the detached list mixes stages).
[[nodiscard]] bool ValidateStageModules(const std::vector<ParticleModule>& modules,
                                           const std::optional<ParticleStage> expectedStage,
                                           const std::vector<ParticleUserParameter>& parameters,
                                           const std::size_t maximumCount = kMaximumParticleModulesPerStage) noexcept {
    if (modules.size() > maximumCount) {
        return false;
    }
    for (const ParticleModule& mod : modules) {
        const ParticleModuleDescriptor* desc = FindParticleModuleDescriptor(mod.type);
        if (desc == nullptr || (expectedStage.has_value() && desc->stage != *expectedStage)) {
            return false;
        }
        if (!std::isfinite(mod.graphX) || !std::isfinite(mod.graphY)) {
            return false;
        }
        for (const auto& [name, val] : mod.floats) {
            if (!desc->defaultFloats.contains(name) || !ValidateFloatInput(val, parameters)) {
                return false;
            }
        }
        for (const auto& [name, val] : mod.vectors) {
            if (!desc->defaultVectors.contains(name) || !ValidateVectorInput(val, parameters)) {
                return false;
            }
        }
        for (const auto& [name, val] : mod.colors) {
            if (!desc->defaultColors.contains(name) || !ValidateColorInput(val, parameters)) {
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

[[nodiscard]] std::string_view ParameterTypeToJson(const ParticleUserParameterType type) noexcept {
    switch (type) {
        case ParticleUserParameterType::Float: return "float";
        case ParticleUserParameterType::Vector: return "vector";
        case ParticleUserParameterType::Color: return "color";
        case ParticleUserParameterType::Bool: return "bool";
        case ParticleUserParameterType::Int: return "int";
    }
    return "float";
}

[[nodiscard]] bool TryParseParameterType(const std::string_view text,
                                            ParticleUserParameterType& outType) noexcept {
    if (text == "float") {
        outType = ParticleUserParameterType::Float;
        return true;
    }
    if (text == "vector") {
        outType = ParticleUserParameterType::Vector;
        return true;
    }
    if (text == "color") {
        outType = ParticleUserParameterType::Color;
        return true;
    }
    if (text == "bool") {
        outType = ParticleUserParameterType::Bool;
        return true;
    }
    if (text == "int") {
        outType = ParticleUserParameterType::Int;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view SpaceToJson(const ParticleSimSpace space) noexcept {
    return space == ParticleSimSpace::Local ? "local" : "world";
}

[[nodiscard]] bool TryParseSpace(const std::string_view text, ParticleSimSpace& outSpace) noexcept {
    if (text == "world") {
        outSpace = ParticleSimSpace::World;
        return true;
    }
    if (text == "local") {
        outSpace = ParticleSimSpace::Local;
        return true;
    }
    return false;
}

[[nodiscard]] std::string_view TargetToJson(const ParticleSimTarget target) noexcept {
    switch (target) {
        case ParticleSimTarget::Cpu: return "cpu";
        case ParticleSimTarget::Gpu: return "gpu";
        case ParticleSimTarget::Auto: return "auto";
    }
    return "cpu";
}

[[nodiscard]] bool TryParseTarget(const std::string_view text, ParticleSimTarget& outTarget) noexcept {
    if (text == "cpu") {
        outTarget = ParticleSimTarget::Cpu;
        return true;
    }
    if (text == "gpu") {
        outTarget = ParticleSimTarget::Gpu;
        return true;
    }
    if (text == "auto") {
        outTarget = ParticleSimTarget::Auto;
        return true;
    }
    return false;
}

[[nodiscard]] Json SerializeVec3(const Math::Vector3& v) {
    return Json::array({v.x, v.y, v.z});
}

[[nodiscard]] bool TryParseVec3(const Json& j, Math::Vector3& out) {
    if (!j.is_array() || j.size() != 3U) {
        return false;
    }
    out = Math::Vector3{j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    return true;
}

[[nodiscard]] Json SerializeFloatInput(const ParticleFloatInput& input) {
    Json j = Json::object();
    switch (input.kind) {
        case ParticleValueKind::Constant:
            j["constant"] = input.constant;
            break;
        case ParticleValueKind::RandomRange:
            j["random"] = Json::array({input.min, input.max});
            break;
        case ParticleValueKind::Curve: {
            Json keys = Json::array();
            Json tangents = Json::array();
            bool hasTangents = false;
            for (const ParticleCurveKey& k : input.curve.keys) {
                keys.push_back(Json::array({k.time, k.value}));
                tangents.push_back(Json::array({k.inTangent, k.outTangent}));
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
        case ParticleValueKind::UserParameter:
            j["parameter"] = input.parameter;
            if (input.scale != 1.0F) {
                j["scale"] = input.scale;
            }
            break;
        case ParticleValueKind::Wire:
            j["wire"] = input.wireNode;
            if (input.scale != 1.0F) {
                j["scale"] = input.scale;
            }
            break;
        case ParticleValueKind::Gradient:
            break;
    }
    return j;
}

[[nodiscard]] bool TryParseFloatInput(const Json& j, ParticleFloatInput& out) {
    if (!j.is_object()) {
        return false;
    }
    out = ParticleFloatInput{};
    if (j.contains("constant")) {
        if (j.size() != 1U) {
            return false;
        }
        out.kind = ParticleValueKind::Constant;
        out.constant = j.at("constant").get<float>();
        return true;
    }
    if (j.contains("random")) {
        if (j.size() != 1U || !j.at("random").is_array() || j.at("random").size() != 2U) {
            return false;
        }
        out.kind = ParticleValueKind::RandomRange;
        out.min = j.at("random")[0].get<float>();
        out.max = j.at("random")[1].get<float>();
        return true;
    }
    if (j.contains("curve")) {
        out.kind = ParticleValueKind::Curve;
        for (const auto& item : j.items()) {
            const std::string& k = item.key();
            if (k != "curve" && k != "smooth" && k != "tangents" && k != "scale") {
                return false;
            }
        }
        const Json& keys = j.at("curve");
        if (!keys.is_array() || keys.size() > kMaximumCurveKeys) {
            return false;
        }
        out.curve.smooth = j.value("smooth", true);
        out.scale = j.value("scale", 1.0F);
        out.curve.keys.reserve(keys.size());
        for (const Json& k : keys) {
            if (!k.is_array() || k.size() != 2U) {
                return false;
            }
            out.curve.keys.push_back(ParticleCurveKey{k[0].get<float>(), k[1].get<float>(), 0.0F, 0.0F});
        }
        if (j.contains("tangents")) {
            const Json& tangents = j.at("tangents");
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
        out.kind = ParticleValueKind::UserParameter;
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
        out.kind = ParticleValueKind::Wire;
        out.wireNode = j.at("wire").get<std::uint32_t>();
        out.scale = j.value("scale", 1.0F);
        return true;
    }
    return false;
}

[[nodiscard]] Json SerializeVectorInput(const ParticleVectorInput& input) {
    Json j = Json::object();
    switch (input.kind) {
        case ParticleValueKind::Constant:
            j["constant"] = SerializeVec3(input.constant);
            break;
        case ParticleValueKind::RandomRange:
            j["random"] = Json::array({SerializeVec3(input.min), SerializeVec3(input.max)});
            break;
        case ParticleValueKind::UserParameter:
            j["parameter"] = input.parameter;
            break;
        case ParticleValueKind::Wire:
            j["wire"] = input.wireNode;
            break;
        case ParticleValueKind::Curve:
        case ParticleValueKind::Gradient:
            break;
    }
    return j;
}

[[nodiscard]] bool TryParseVectorInput(const Json& j, ParticleVectorInput& out) {
    if (!j.is_object() || j.size() != 1U) {
        return false;
    }
    out = ParticleVectorInput{};
    if (j.contains("constant")) {
        out.kind = ParticleValueKind::Constant;
        return TryParseVec3(j.at("constant"), out.constant);
    }
    if (j.contains("random")) {
        const Json& r = j.at("random");
        if (!r.is_array() || r.size() != 2U) {
            return false;
        }
        out.kind = ParticleValueKind::RandomRange;
        return TryParseVec3(r[0], out.min) && TryParseVec3(r[1], out.max);
    }
    if (j.contains("parameter")) {
        out.kind = ParticleValueKind::UserParameter;
        out.parameter = j.at("parameter").get<std::string>();
        return true;
    }
    if (j.contains("wire")) {
        out.kind = ParticleValueKind::Wire;
        out.wireNode = j.at("wire").get<std::uint32_t>();
        return true;
    }
    return false;
}

[[nodiscard]] Json SerializeColorInput(const ParticleColorInput& input) {
    Json j = Json::object();
    switch (input.kind) {
        case ParticleValueKind::Constant:
            j["constant"] = Json::array({input.constant.r, input.constant.g, input.constant.b, input.alpha});
            break;
        case ParticleValueKind::RandomRange:
            j["random"] = Json::array({
                Json::array({input.minColor.r, input.minColor.g, input.minColor.b}),
                Json::array({input.maxColor.r, input.maxColor.g, input.maxColor.b}),
            });
            if (input.alpha != 1.0F) {
                j["alpha"] = input.alpha;
            }
            break;
        case ParticleValueKind::Gradient: {
            Json stops = Json::array();
            for (const ParticleGradientStop& s : input.gradient.stops) {
                stops.push_back(Json::array({s.time, s.color.r, s.color.g, s.color.b, s.alpha}));
            }
            j["gradient"] = std::move(stops);
            break;
        }
        case ParticleValueKind::UserParameter:
            j["parameter"] = input.parameter;
            break;
        case ParticleValueKind::Curve:
        case ParticleValueKind::Wire:
            break;
    }
    return j;
}

[[nodiscard]] bool TryParseColorInput(const Json& j, ParticleColorInput& out) {
    if (!j.is_object()) {
        return false;
    }
    out = ParticleColorInput{};
    if (j.contains("constant")) {
        if (j.size() != 1U) {
            return false;
        }
        const Json& c = j.at("constant");
        if (!c.is_array() || (c.size() != 3U && c.size() != 4U)) {
            return false;
        }
        out.kind = ParticleValueKind::Constant;
        out.constant = Math::Color{c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
        out.alpha = c.size() == 4U ? c[3].get<float>() : 1.0F;
        return true;
    }
    if (j.contains("random")) {
        for (const auto& item : j.items()) {
            if (item.key() != "random" && item.key() != "alpha") {
                return false;
            }
        }
        const Json& r = j.at("random");
        if (!r.is_array() || r.size() != 2U || !r[0].is_array() || !r[1].is_array() ||
            r[0].size() < 3U || r[0].size() > 4U || r[1].size() < 3U || r[1].size() > 4U) {
            return false;
        }
        out.kind = ParticleValueKind::RandomRange;
        out.minColor = Math::Color{r[0][0].get<float>(), r[0][1].get<float>(), r[0][2].get<float>()};
        out.maxColor = Math::Color{r[1][0].get<float>(), r[1][1].get<float>(), r[1][2].get<float>()};
        out.alpha = j.value("alpha", 1.0F);
        return true;
    }
    if (j.contains("gradient")) {
        if (j.size() != 1U || !j.at("gradient").is_array() ||
            j.at("gradient").size() > kMaximumGradientStops) {
            return false;
        }
        out.kind = ParticleValueKind::Gradient;
        out.gradient.stops.reserve(j.at("gradient").size());
        for (const Json& s : j.at("gradient")) {
            if (!s.is_array() || s.size() != 5U) {
                return false;
            }
            out.gradient.stops.push_back(ParticleGradientStop{
                s[0].get<float>(),
                Math::Color{s[1].get<float>(), s[2].get<float>(), s[3].get<float>()},
                s[4].get<float>(),
            });
        }
        return true;
    }
    if (j.contains("parameter")) {
        if (j.size() != 1U) {
            return false;
        }
        out.kind = ParticleValueKind::UserParameter;
        out.parameter = j.at("parameter").get<std::string>();
        return true;
    }
    return false;
}

[[nodiscard]] Json SerializeModules(const std::vector<ParticleModule>& modules) {
    Json array = Json::array();
    for (const ParticleModule& mod : modules) {
        const ParticleModuleDescriptor* desc = FindParticleModuleDescriptor(mod.type);
        if (desc == nullptr) {
            continue;
        }
        Json entry{{"id", mod.id}, {"type", desc->canonicalName}};
        if (!mod.enabled) {
            entry["enabled"] = false;
        }

        Json floats = Json::object();
        for (const auto& [name, val] : mod.floats) {
            const auto defIt = desc->defaultFloats.find(name);
            if (defIt != desc->defaultFloats.end() && defIt->second == val) {
                continue;
            }
            floats[name] = SerializeFloatInput(val);
        }
        if (!floats.empty()) {
            entry["floats"] = std::move(floats);
        }

        Json vectors = Json::object();
        for (const auto& [name, val] : mod.vectors) {
            const auto defIt = desc->defaultVectors.find(name);
            if (defIt != desc->defaultVectors.end() && defIt->second == val) {
                continue;
            }
            vectors[name] = SerializeVectorInput(val);
        }
        if (!vectors.empty()) {
            entry["vectors"] = std::move(vectors);
        }

        Json colors = Json::object();
        for (const auto& [name, val] : mod.colors) {
            const auto defIt = desc->defaultColors.find(name);
            if (defIt != desc->defaultColors.end() && defIt->second == val) {
                continue;
            }
            colors[name] = SerializeColorInput(val);
        }
        if (!colors.empty()) {
            entry["colors"] = std::move(colors);
        }

        Json ints = Json::object();
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

        Json strings = Json::object();
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

        if (mod.graphPosAtOrigin || mod.graphX != 0.0F || mod.graphY != 0.0F) {
            entry["at"] = Json::array({mod.graphX, mod.graphY});
        }
        array.push_back(std::move(entry));
    }
    return array;
}

[[nodiscard]] bool TryParseModules(const Json& emitterJson, const std::string_view stageKey,
                                      const std::optional<ParticleStage> expectedStage,
                                      std::vector<ParticleModule>& outModules,
                                      const std::size_t maximumCount = kMaximumParticleModulesPerStage) {
    const std::string keyString(stageKey);
    if (!emitterJson.contains(keyString)) {
        return true;
    }
    const Json& list = emitterJson.at(keyString);
    if (!list.is_array() || list.size() > maximumCount) {
        return false;
    }
    outModules.reserve(list.size());
    for (const Json& item : list) {
        if (!item.is_object() || !item.contains("type")) {
            return false;
        }
        const std::string typeName = item.at("type").get<std::string>();
        const ParticleModuleDescriptor* desc = FindParticleModuleDescriptorByName(typeName);
        if (desc == nullptr || (expectedStage.has_value() && desc->stage != *expectedStage)) {
            return false;
        }
        ParticleModule mod;
        mod.id = item.value("id", 0U);
        mod.type = desc->type;
        mod.enabled = item.value("enabled", true);
        if (item.contains("at")) {
            const Json& at = item.at("at");
            if (!at.is_array() || at.size() != 2U) {
                return false;
            }
            mod.graphX = at[0].get<float>();
            mod.graphY = at[1].get<float>();
            mod.graphPosAtOrigin = (std::abs(mod.graphX) < 1.0e-4F && std::abs(mod.graphY) < 1.0e-4F);
        }
        if (item.contains("floats")) {
            const Json& floats = item.at("floats");
            if (!floats.is_object()) {
                return false;
            }
            for (const auto& [k, v] : floats.items()) {
                if (!desc->defaultFloats.contains(k)) {
                    return false;
                }
                ParticleFloatInput parsed;
                if (!TryParseFloatInput(v, parsed)) {
                    return false;
                }
                mod.floats.emplace(k, std::move(parsed));
            }
        }
        if (item.contains("vectors")) {
            const Json& vectors = item.at("vectors");
            if (!vectors.is_object()) {
                return false;
            }
            for (const auto& [k, v] : vectors.items()) {
                if (!desc->defaultVectors.contains(k)) {
                    return false;
                }
                ParticleVectorInput parsed;
                if (!TryParseVectorInput(v, parsed)) {
                    return false;
                }
                mod.vectors.emplace(k, std::move(parsed));
            }
        }
        if (item.contains("colors")) {
            const Json& colors = item.at("colors");
            if (!colors.is_object()) {
                return false;
            }
            for (const auto& [k, v] : colors.items()) {
                if (!desc->defaultColors.contains(k)) {
                    return false;
                }
                ParticleColorInput parsed;
                if (!TryParseColorInput(v, parsed)) {
                    return false;
                }
                mod.colors.emplace(k, std::move(parsed));
            }
        }
        if (item.contains("ints")) {
            const Json& ints = item.at("ints");
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
            const Json& strings = item.at("strings");
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

const std::vector<ParticleModuleDescriptor>& GetParticleModuleCatalog() noexcept {
    static const std::vector<ParticleModuleDescriptor> catalog = BuildModuleCatalog();
    return catalog;
}

const ParticleModuleDescriptor* FindParticleModuleDescriptor(const ParticleModuleType type) noexcept {
    const auto& catalog = GetParticleModuleCatalog();
    for (const ParticleModuleDescriptor& entry : catalog) {
        if (entry.type == type) {
            return &entry;
        }
    }
    return nullptr;
}

const ParticleModuleDescriptor* FindParticleModuleDescriptorByName(
    const std::string_view canonicalName) noexcept {
    const auto& catalog = GetParticleModuleCatalog();
    for (const ParticleModuleDescriptor& entry : catalog) {
        if (entry.canonicalName == canonicalName) {
            return &entry;
        }
    }
    return nullptr;
}

ParticleModule MakeDefaultParticleModule(const std::uint32_t id, const ParticleModuleType type,
                                               const float graphX, const float graphY) {
    ParticleModule mod;
    mod.id = id;
    mod.type = type;
    mod.enabled = true;
    mod.graphX = graphX;
    mod.graphY = graphY;
    return mod;
}

bool IsParticleSystemAssetValid(const ParticleSystemAsset& system) noexcept {
    if (system.emitters.empty() || system.emitters.size() > kMaximumParticleEmitters ||
        system.parameters.size() > kMaximumParticleUserParameters) {
        return false;
    }
    if (!std::isfinite(system.duration) || system.duration <= 0.0F ||
        !std::isfinite(system.warmUpSeconds) || system.warmUpSeconds < 0.0F) {
        return false;
    }
    if (!Math::IsFinite(system.fixedBounds.min) || !Math::IsFinite(system.fixedBounds.max) ||
        system.fixedBounds.min.x > system.fixedBounds.max.x ||
        system.fixedBounds.min.y > system.fixedBounds.max.y ||
        system.fixedBounds.min.z > system.fixedBounds.max.z) {
        return false;
    }
    for (const ParticleScalability& tier : system.scalability) {
        if (!std::isfinite(tier.spawnScale) || tier.spawnScale < 0.0F ||
            !std::isfinite(tier.cullDistance) || tier.cullDistance < 0.0F) {
            return false;
        }
    }

    std::set<std::string> paramNames;
    for (const ParticleUserParameter& param : system.parameters) {
        if (!NameOk(param.name) ||
            static_cast<std::uint8_t>(param.type) > static_cast<std::uint8_t>(ParticleUserParameterType::Int) ||
            !Math::IsFinite(param.value) || !std::isfinite(param.alpha) ||
            !paramNames.insert(param.name).second) {
            return false;
        }
    }

    std::set<std::string> emitterNames;
    for (const ParticleEmitterAsset& emitter : system.emitters) {
        if (!NameOk(emitter.name) || !emitterNames.insert(emitter.name).second) {
            return false;
        }
        if (static_cast<std::uint8_t>(emitter.space) > static_cast<std::uint8_t>(ParticleSimSpace::Local) ||
            static_cast<std::uint8_t>(emitter.target) > static_cast<std::uint8_t>(ParticleSimTarget::Auto)) {
            return false;
        }
        if (emitter.maxParticles == 0U || emitter.maxParticles > kMaximumParticlesPerEmitter) {
            return false;
        }
        if (!ValidateStageModules(emitter.emitterUpdate, ParticleStage::EmitterUpdate, system.parameters) ||
            !ValidateStageModules(emitter.particleSpawn, ParticleStage::ParticleSpawn, system.parameters) ||
            !ValidateStageModules(emitter.particleUpdate, ParticleStage::ParticleUpdate, system.parameters) ||
            !ValidateStageModules(emitter.render, ParticleStage::Render, system.parameters) ||
            !ValidateStageModules(emitter.detached, std::nullopt, system.parameters,
                                     kMaximumParticleDetachedModules)) {
            return false;
        }
        if (emitter.mathNodes.size() > kMaximumParticleMathNodes ||
            emitter.comments.size() > kMaximumParticleComments ||
            emitter.detachedLinks.size() > kMaximumParticleDetachedLinks) {
            return false;
        }
        for (const ParticleDetachedLink& link : emitter.detachedLinks) {
            if (link.fromModuleId == 0U || link.toModuleId == 0U || link.fromModuleId == link.toModuleId) {
                return false;
            }
        }
        for (const ParticleStageNodePos& stagePos : emitter.stageNodePositions) {
            if (!std::isfinite(stagePos.x) || !std::isfinite(stagePos.y)) {
                return false;
            }
        }
        for (const ParticleMathNode& math : emitter.mathNodes) {
            if (!NameOk(math.op) || !std::isfinite(math.constant) ||
                !std::isfinite(math.graphX) || !std::isfinite(math.graphY) ||
                math.inputs.size() > 16U) {
                return false;
            }
        }
    }
    return true;
}

bool SaveParticleSystemAsset(const ParticleSystemAsset& system, const std::filesystem::path& path) {
    if (!IsParticleSystemAssetValid(system)) {
        UVE_ERROR("ParticleSystemAsset: refused to save invalid particle system to {}", path.string());
        return false;
    }

    Json document;
    document["schema"] = kParticleSystemSchema;
    document["duration"] = system.duration;
    document["looping"] = system.looping;
    document["warmUp"] = system.warmUpSeconds;
    document["bounds"] = {
        {"min", SerializeVec3(system.fixedBounds.min)},
        {"max", SerializeVec3(system.fixedBounds.max)},
    };

    if (!system.parameters.empty()) {
        Json params = Json::array();
        for (const ParticleUserParameter& param : system.parameters) {
            Json p{
                {"name", param.name},
                {"type", ParameterTypeToJson(param.type)},
                {"value", SerializeVec3(param.value)},
            };
            if (param.type == ParticleUserParameterType::Color || param.alpha != 1.0F) {
                p["alpha"] = param.alpha;
            }
            params.push_back(std::move(p));
        }
        document["parameters"] = std::move(params);
    }

    const ParticleScalability defaultTier{};
    bool anyScalabilityNonDefault = false;
    Json scalability = Json::array();
    for (const ParticleScalability& tier : system.scalability) {
        Json t = Json::object();
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

    constexpr std::array<const char*, 4> kStagePosKeys{"emitterUpdate", "particleSpawn", "particleUpdate", "render"};
    Json emitters = Json::array();
    for (const ParticleEmitterAsset& emitter : system.emitters) {
        Json e{
            {"name", emitter.name},
            {"space", SpaceToJson(emitter.space)},
            {"target", TargetToJson(emitter.target)},
            {"maxParticles", emitter.maxParticles},
        };
        if (!emitter.enabled) {
            e["enabled"] = false;
        }
        if (emitter.seed != 0U) {
            e["seed"] = emitter.seed;
        }
        if (!emitter.emitterUpdate.empty()) {
            e["emitterUpdate"] = SerializeModules(emitter.emitterUpdate);
        }
        if (!emitter.particleSpawn.empty()) {
            e["particleSpawn"] = SerializeModules(emitter.particleSpawn);
        }
        if (!emitter.particleUpdate.empty()) {
            e["particleUpdate"] = SerializeModules(emitter.particleUpdate);
        }
        if (!emitter.render.empty()) {
            e["render"] = SerializeModules(emitter.render);
        }
        if (!emitter.detached.empty()) {
            e["detached"] = SerializeModules(emitter.detached);
        }
        if (!emitter.detachedLinks.empty()) {
            Json links = Json::array();
            for (const ParticleDetachedLink& link : emitter.detachedLinks) {
                links.push_back(Json::array({link.fromModuleId, link.toModuleId}));
            }
            e["detachedLinks"] = std::move(links);
        }
        Json stageNodes = Json::object();
        for (std::size_t si = 0U; si < emitter.stageNodePositions.size(); ++si) {
            if (emitter.stageNodePositions[si].placed) {
                stageNodes[kStagePosKeys[si]] = Json::array({
                    emitter.stageNodePositions[si].x,
                    emitter.stageNodePositions[si].y,
                });
            }
        }
        if (!stageNodes.empty()) {
            e["stageNodes"] = std::move(stageNodes);
        }
        if (!emitter.mathNodes.empty()) {
            Json nodes = Json::array();
            for (const ParticleMathNode& math : emitter.mathNodes) {
                Json n{
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
                    n["at"] = Json::array({math.graphX, math.graphY});
                }
                nodes.push_back(std::move(n));
            }
            e["mathNodes"] = std::move(nodes);
        }
        if (!emitter.comments.empty()) {
            Json comments = Json::array();
            for (const auto& [a, b] : emitter.comments) {
                comments.push_back(Json::array({a, b}));
            }
            e["comments"] = std::move(comments);
        }
        emitters.push_back(std::move(e));
    }
    document["emitters"] = std::move(emitters);

    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumParticleSystemPayloadBytes) {
        UVE_ERROR("ParticleSystemAsset: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFile(path, AssetKind::ParticleSystem,
                           std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadParticleSystemAsset(const std::filesystem::path& path, ParticleSystemAsset& outSystem) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value() || file->first.assetType != AssetKind::ParticleSystem || file->second.empty() ||
        file->second.size() > kMaximumParticleSystemPayloadBytes) {
        UVE_ERROR("ParticleSystemAsset: invalid particle system envelope {}", path.string());
        return false;
    }

    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const Json document = Json::parse(serialized);
        if (!document.is_object() || document.value("schema", std::string{}) != kParticleSystemSchema ||
            !document.contains("emitters") || !document.at("emitters").is_array()) {
            UVE_ERROR("ParticleSystemAsset: invalid schema or emitters in {}", path.string());
            return false;
        }

        ParticleSystemAsset candidate;
        candidate.duration = document.value("duration", candidate.duration);
        candidate.looping = document.value("looping", candidate.looping);
        candidate.warmUpSeconds = document.value("warmUp", candidate.warmUpSeconds);
        if (document.contains("bounds")) {
            const Json& bounds = document.at("bounds");
            if (!bounds.is_object() || !bounds.contains("min") || !bounds.contains("max") ||
                !TryParseVec3(bounds.at("min"), candidate.fixedBounds.min) ||
                !TryParseVec3(bounds.at("max"), candidate.fixedBounds.max)) {
                return false;
            }
        }

        if (document.contains("parameters")) {
            const Json& params = document.at("parameters");
            if (!params.is_array() || params.size() > kMaximumParticleUserParameters) {
                return false;
            }
            candidate.parameters.reserve(params.size());
            for (const Json& p : params) {
                if (!p.is_object() || !p.contains("name") || !p.contains("type")) {
                    return false;
                }
                ParticleUserParameter param;
                param.name = p.at("name").get<std::string>();
                if (!TryParseParameterType(p.at("type").get<std::string>(), param.type)) {
                    return false;
                }
                if (p.contains("value")) {
                    const Json& val = p.at("value");
                    if (val.is_array()) {
                        if (!TryParseVec3(val, param.value)) {
                            return false;
                        }
                    } else if (val.is_number()) {
                        param.value = Math::Vector3{val.get<float>(), 0.0F, 0.0F};
                    } else if (val.is_boolean()) {
                        param.value = Math::Vector3{val.get<bool>() ? 1.0F : 0.0F, 0.0F, 0.0F};
                    } else {
                        return false;
                    }
                }
                param.alpha = p.value("alpha", 1.0F);
                candidate.parameters.push_back(std::move(param));
            }
        }

        if (document.contains("scalability")) {
            const Json& tiers = document.at("scalability");
            if (!tiers.is_array() || tiers.size() > candidate.scalability.size()) {
                return false;
            }
            for (std::size_t idx = 0U; idx < tiers.size(); ++idx) {
                const Json& t = tiers[idx];
                if (!t.is_object()) {
                    return false;
                }
                candidate.scalability[idx].spawnScale = t.value("spawnScale", 1.0F);
                candidate.scalability[idx].maxParticlesCap = t.value("maxParticles", 0U);
                candidate.scalability[idx].cullDistance = t.value("cullDistance", 0.0F);
            }
        }

        const Json& emitters = document.at("emitters");
        if (emitters.empty() || emitters.size() > kMaximumParticleEmitters) {
            return false;
        }
        constexpr std::array<const char*, 4> kStagePosKeys{"emitterUpdate", "particleSpawn", "particleUpdate", "render"};
        candidate.emitters.reserve(emitters.size());
        for (const Json& e : emitters) {
            if (!e.is_object() || !e.contains("name")) {
                return false;
            }
            ParticleEmitterAsset emitter;
            emitter.name = e.at("name").get<std::string>();
            emitter.enabled = e.value("enabled", true);
            if (e.contains("space") && !TryParseSpace(e.at("space").get<std::string>(), emitter.space)) {
                return false;
            }
            if (e.contains("target") && !TryParseTarget(e.at("target").get<std::string>(), emitter.target)) {
                return false;
            }
            emitter.maxParticles = e.value("maxParticles", emitter.maxParticles);
            emitter.seed = e.value("seed", 0U);
            if (!TryParseModules(e, "emitterUpdate", ParticleStage::EmitterUpdate, emitter.emitterUpdate) ||
                !TryParseModules(e, "particleSpawn", ParticleStage::ParticleSpawn, emitter.particleSpawn) ||
                !TryParseModules(e, "particleUpdate", ParticleStage::ParticleUpdate, emitter.particleUpdate) ||
                !TryParseModules(e, "render", ParticleStage::Render, emitter.render) ||
                !TryParseModules(e, "detached", std::nullopt, emitter.detached,
                                    kMaximumParticleDetachedModules)) {
                return false;
            }
            if (e.contains("detachedLinks")) {
                const Json& links = e.at("detachedLinks");
                if (!links.is_array() || links.size() > kMaximumParticleDetachedLinks) {
                    return false;
                }
                emitter.detachedLinks.reserve(links.size());
                for (const Json& link : links) {
                    if (!link.is_array() || link.size() != 2U ||
                        !link[0].is_number_unsigned() || !link[1].is_number_unsigned()) {
                        return false;
                    }
                    emitter.detachedLinks.push_back(
                        ParticleDetachedLink{link[0].get<std::uint32_t>(), link[1].get<std::uint32_t>()});
                }
            }
            if (e.contains("stageNodes")) {
                const Json& stageNodes = e.at("stageNodes");
                if (!stageNodes.is_object()) {
                    return false;
                }
                for (const auto& [k, v] : stageNodes.items()) {
                    std::size_t stageIdx = kStagePosKeys.size();
                    for (std::size_t si = 0U; si < kStagePosKeys.size(); ++si) {
                        if (k == kStagePosKeys[si]) {
                            stageIdx = si;
                            break;
                        }
                    }
                    if (stageIdx >= kStagePosKeys.size() || !v.is_array() || v.size() != 2U ||
                        !v[0].is_number() || !v[1].is_number()) {
                        return false;
                    }
                    emitter.stageNodePositions[stageIdx].placed = true;
                    emitter.stageNodePositions[stageIdx].x = v[0].get<float>();
                    emitter.stageNodePositions[stageIdx].y = v[1].get<float>();
                }
            }
            if (e.contains("mathNodes")) {
                const Json& nodes = e.at("mathNodes");
                if (!nodes.is_array() || nodes.size() > kMaximumParticleMathNodes) {
                    return false;
                }
                for (const Json& n : nodes) {
                    if (!n.is_object() || !n.contains("op")) {
                        return false;
                    }
                    ParticleMathNode math;
                    math.id = n.value("id", 0U);
                    math.op = n.at("op").get<std::string>();
                    math.inputs = n.value("inputs", std::vector<std::uint32_t>{});
                    math.constant = n.value("constant", 0.0F);
                    if (n.contains("at")) {
                        const Json& at = n.at("at");
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
                const Json& comments = e.at("comments");
                if (!comments.is_array() || comments.size() > kMaximumParticleComments) {
                    return false;
                }
                for (const Json& c : comments) {
                    if (!c.is_array() || c.size() != 2U) {
                        return false;
                    }
                    emitter.comments.emplace_back(c[0].get<std::uint32_t>(), c[1].get<std::uint32_t>());
                }
            }
            candidate.emitters.push_back(std::move(emitter));
        }

        if (!IsParticleSystemAssetValid(candidate)) {
            UVE_ERROR("ParticleSystemAsset: loaded system failed validation in {}", path.string());
            return false;
        }
        outSystem = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("ParticleSystemAsset: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("ParticleSystemAsset: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
