// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aabb.h"
#include "color.h"
#include "vector3.h"

namespace UVE::Asset {

inline constexpr std::size_t kMaximumParticleEmittersUVE = 32U;
inline constexpr std::size_t kMaximumParticleModulesPerStageUVE = 64U;
inline constexpr std::size_t kMaximumParticleUserParametersUVE = 64U;
inline constexpr std::uint32_t kMaximumParticlesPerEmitterUVE = 1'000'000U;
inline constexpr std::size_t kMaximumCurveKeysUVE = 64U;
inline constexpr std::size_t kMaximumGradientStopsUVE = 16U;
inline constexpr std::size_t kMaximumParticleSystemPayloadBytesUVE = 8U * 1024U * 1024U;
inline constexpr std::size_t kMaximumParticleNameBytesUVE = 64U;

enum class ParticleValueKindUVE : std::uint8_t {
    Constant = 0,
    RandomRange,
    Curve,
    Gradient,
    UserParameter,
    Wire,
};

struct ParticleCurveKeyUVE final {
    float time = 0.0F;
    float value = 0.0F;
    float inTangent = 0.0F;
    float outTangent = 0.0F;
    [[nodiscard]] bool operator==(const ParticleCurveKeyUVE&) const = default;
};

struct ParticleCurveUVE final {
    std::vector<ParticleCurveKeyUVE> keys;
    bool smooth = true; ///< Hermite with tangents; false = linear.
    [[nodiscard]] bool operator==(const ParticleCurveUVE&) const = default;
};

struct ParticleGradientStopUVE final {
    float time = 0.0F;
    Math::ColorUVE color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    [[nodiscard]] bool operator==(const ParticleGradientStopUVE&) const = default;
};

struct ParticleGradientUVE final {
    std::vector<ParticleGradientStopUVE> stops;
    [[nodiscard]] bool operator==(const ParticleGradientUVE&) const = default;
};

/// A float input. `curve` is sampled at the particle's normalized age (or the emitter's, in Emitter
/// Update), then multiplied by `scale`.
struct ParticleFloatInputUVE final {
    ParticleValueKindUVE kind = ParticleValueKindUVE::Constant;
    float constant = 0.0F;
    float min = 0.0F;
    float max = 0.0F; ///< RandomRange: uniform per particle (per spawn).
    ParticleCurveUVE curve;
    float scale = 1.0F;
    std::string parameter;     ///< UserParameter name.
    std::uint32_t wireNode = 0U; ///< Wire: the Math/Value node id feeding it.
    [[nodiscard]] bool operator==(const ParticleFloatInputUVE&) const = default;
};

struct ParticleVectorInputUVE final {
    ParticleValueKindUVE kind = ParticleValueKindUVE::Constant;
    Math::Vector3UVE constant{};
    Math::Vector3UVE min{};
    Math::Vector3UVE max{};
    std::string parameter;
    std::uint32_t wireNode = 0U;
    [[nodiscard]] bool operator==(const ParticleVectorInputUVE&) const = default;
};

struct ParticleColorInputUVE final {
    ParticleValueKindUVE kind = ParticleValueKindUVE::Constant;
    Math::ColorUVE constant{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    Math::ColorUVE minColor{1.0F, 1.0F, 1.0F};
    Math::ColorUVE maxColor{1.0F, 1.0F, 1.0F};
    ParticleGradientUVE gradient;
    std::string parameter;
    [[nodiscard]] bool operator==(const ParticleColorInputUVE&) const = default;
};

enum class ParticleModuleTypeUVE : std::uint16_t {
    // Emitter Update
    SpawnRate = 0,
    SpawnBurst,
    SpawnPerDistance,
    // Particle Spawn
    ShapePoint,
    ShapeSphere,
    ShapeBox,
    ShapeCone,
    ShapeCylinder,
    ShapeTorus,
    ShapeMeshSurface,
    InitialVelocity,
    VelocityInCone,
    InitialLifetime,
    InitialColor,
    InitialSize,
    InitialRotation,
    InitialSpin,
    InitialSpriteFrame,
    InitialMass,
    InheritOwnerVelocity,
    // Particle Update
    Gravity,
    Drag,
    AddVelocity,
    CurlNoise,
    Vortex,
    PointAttractor,
    Wind,
    LimitSpeed,
    Orbit,
    ColorOverLife,
    SizeOverLife,
    RotationRate,
    SizeBySpeed,
    SpriteFlipbook,
    CollisionPlane,
    CollisionDepth,
    CollisionPhysics,
    KillByAge,
    KillByBounds,
    KillByCondition,
    EventOnDeath,
    EventOnCollision,
    // Render
    SpriteRenderer,
    RibbonRenderer,
    MeshRenderer,
    LightRenderer,
};

/// One module: its type, whether it runs, and its inputs by name.
/// Unknown names are a load error; a missing name takes the module's default.
struct ParticleModuleUVE final {
    std::uint32_t id = 0U; ///< Stable within the emitter (graph node id).
    ParticleModuleTypeUVE type{};
    bool enabled = true;
    std::map<std::string, ParticleFloatInputUVE> floats;
    std::map<std::string, ParticleVectorInputUVE> vectors;
    std::map<std::string, ParticleColorInputUVE> colors;
    std::map<std::string, std::int32_t> ints;   ///< Enums and counts.
    std::map<std::string, std::string> strings; ///< Asset paths (texture, mesh, material), emitter names.
    float graphX = 0.0F;
    float graphY = 0.0F;
    [[nodiscard]] bool operator==(const ParticleModuleUVE&) const = default;
};

enum class ParticleSimSpaceUVE : std::uint8_t {
    World = 0,
    Local,
};

enum class ParticleSimTargetUVE : std::uint8_t {
    Cpu = 0,
    Gpu,
    Auto,
};

struct ParticleMathNodeUVE final {
    std::uint32_t id = 0U;
    std::string op;
    std::vector<std::uint32_t> inputs;
    float constant = 0.0F;
    float graphX = 0.0F;
    float graphY = 0.0F;
    [[nodiscard]] bool operator==(const ParticleMathNodeUVE&) const = default;
};

struct ParticleEmitterAssetUVE final {
    std::string name = "Emitter";
    bool enabled = true;
    ParticleSimSpaceUVE space = ParticleSimSpaceUVE::World;
    ParticleSimTargetUVE target = ParticleSimTargetUVE::Cpu;
    std::uint32_t maxParticles = 1000U;
    std::uint32_t seed = 0U; ///< 0: derived from the instance seed.
    std::vector<ParticleModuleUVE> emitterUpdate;
    std::vector<ParticleModuleUVE> particleSpawn;
    std::vector<ParticleModuleUVE> particleUpdate;
    std::vector<ParticleModuleUVE> render;
    std::vector<ParticleMathNodeUVE> mathNodes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> comments;
    [[nodiscard]] bool operator==(const ParticleEmitterAssetUVE&) const = default;
};

enum class ParticleUserParameterTypeUVE : std::uint8_t {
    Float = 0,
    Vector,
    Color,
    Bool,
    Int,
};

struct ParticleUserParameterUVE final {
    std::string name;
    ParticleUserParameterTypeUVE type{};
    Math::Vector3UVE value{};
    float alpha = 1.0F;
    [[nodiscard]] bool operator==(const ParticleUserParameterUVE&) const = default;
};

struct ParticleScalabilityUVE final {
    float spawnScale = 1.0F;
    std::uint32_t maxParticlesCap = 0U;
    float cullDistance = 0.0F;
    [[nodiscard]] bool operator==(const ParticleScalabilityUVE&) const = default;
};

struct ParticleSystemAssetUVE final {
    std::vector<ParticleEmitterAssetUVE> emitters;
    std::vector<ParticleUserParameterUVE> parameters;
    float duration = 2.0F; ///< Seconds; one loop.
    bool looping = true;
    float warmUpSeconds = 0.0F;
    Math::AabbUVE fixedBounds{{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}}; ///< Culling; local space.
    std::array<ParticleScalabilityUVE, 3> scalability{};                  ///< Low, Medium, High.
    [[nodiscard]] bool operator==(const ParticleSystemAssetUVE&) const = default;
};

/// Which pipeline stage a module type belongs to.
enum class ParticleStageUVE : std::uint8_t {
    EmitterUpdate = 0,
    ParticleSpawn,
    ParticleUpdate,
    Render,
};

/// Authoritative module metadata shared by the validator, compiler, serializer and editor.
struct ParticleModuleDescriptorUVE final {
    ParticleModuleTypeUVE type{};
    ParticleStageUVE stage = ParticleStageUVE::EmitterUpdate;
    std::string_view canonicalName;
    std::string_view displayName;
    std::string_view description;
    std::map<std::string, ParticleFloatInputUVE> defaultFloats{};
    std::map<std::string, ParticleVectorInputUVE> defaultVectors{};
    std::map<std::string, ParticleColorInputUVE> defaultColors{};
    std::map<std::string, std::int32_t> defaultInts{};
    std::map<std::string, std::string> defaultStrings{};
};

/// Returns the catalog of all built-in particle modules (`vfx_module_catalog`).
[[nodiscard]] const std::vector<ParticleModuleDescriptorUVE>& GetParticleModuleCatalogUVE() noexcept;

/// Looks up metadata for `type`, or nullptr if invalid.
[[nodiscard]] const ParticleModuleDescriptorUVE* FindParticleModuleDescriptorUVE(
    ParticleModuleTypeUVE type) noexcept;

/// Looks up metadata by serialized canonical type name (e.g. `"SpawnRate"`), or nullptr if unknown.
[[nodiscard]] const ParticleModuleDescriptorUVE* FindParticleModuleDescriptorByNameUVE(
    std::string_view canonicalName) noexcept;

/// Constructs a module with its default identity (`id`, `type`, position) and empty override maps;
/// omitted input names fall back to the catalog's defaults.
[[nodiscard]] ParticleModuleUVE MakeDefaultParticleModuleUVE(
    std::uint32_t id, ParticleModuleTypeUVE type, float graphX = 0.0F, float graphY = 0.0F);

/// Returns true iff `system` satisfies every structural, range, name, parameter and catalog rule.
[[nodiscard]] bool IsParticleSystemAssetValidUVE(const ParticleSystemAssetUVE& system) noexcept;

/// Saves `system` to `path` as a `.uvfx` envelope (`AssetKindUVE::ParticleSystem`), omitting inputs
/// that match their module default.
[[nodiscard]] bool SaveParticleSystemAssetUVE(const ParticleSystemAssetUVE& system,
                                              const std::filesystem::path& path);

/// Loads `path` as a `.uvfx` envelope (`AssetKindUVE::ParticleSystem`), validating schema and contents
/// before modifying `outSystem`.
[[nodiscard]] bool LoadParticleSystemAssetUVE(const std::filesystem::path& path,
                                              ParticleSystemAssetUVE& outSystem);

} // namespace UVE::Asset
