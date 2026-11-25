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

inline constexpr std::size_t kMaximumParticleEmitters = 32U;
inline constexpr std::size_t kMaximumParticleModulesPerStage = 64U;
inline constexpr std::size_t kMaximumParticleUserParameters = 64U;
inline constexpr std::size_t kMaximumParticleDetachedModules = 256U;
inline constexpr std::size_t kMaximumParticleDetachedLinks = 512U;
inline constexpr std::uint32_t kMaximumParticlesPerEmitter = 1'000'000U;
inline constexpr std::size_t kMaximumCurveKeys = 64U;
inline constexpr std::size_t kMaximumGradientStops = 16U;
inline constexpr std::size_t kMaximumParticleSystemPayloadBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t kMaximumParticleNameBytes = 64U;

enum class ParticleValueKind : std::uint8_t {
    Constant = 0,
    RandomRange,
    Curve,
    Gradient,
    UserParameter,
    Wire,
};

struct ParticleCurveKey final {
    float time = 0.0F;
    float value = 0.0F;
    float inTangent = 0.0F;
    float outTangent = 0.0F;
    [[nodiscard]] bool operator==(const ParticleCurveKey&) const = default;
};

struct ParticleCurve final {
    std::vector<ParticleCurveKey> keys;
    bool smooth = true; ///< Hermite with tangents; false = linear.
    [[nodiscard]] bool operator==(const ParticleCurve&) const = default;
};

struct ParticleGradientStop final {
    float time = 0.0F;
    Math::Color color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    [[nodiscard]] bool operator==(const ParticleGradientStop&) const = default;
};

struct ParticleGradient final {
    std::vector<ParticleGradientStop> stops;
    [[nodiscard]] bool operator==(const ParticleGradient&) const = default;
};

/// A float input. `curve` is sampled at the particle's normalized age (or the emitter's, in Emitter
/// Update), then multiplied by `scale`.
struct ParticleFloatInput final {
    ParticleValueKind kind = ParticleValueKind::Constant;
    float constant = 0.0F;
    float min = 0.0F;
    float max = 0.0F; ///< RandomRange: uniform per particle (per spawn).
    ParticleCurve curve;
    float scale = 1.0F;
    std::string parameter;     ///< UserParameter name.
    std::uint32_t wireNode = 0U; ///< Wire: the Math/Value node id feeding it.
    [[nodiscard]] bool operator==(const ParticleFloatInput&) const = default;
};

struct ParticleVectorInput final {
    ParticleValueKind kind = ParticleValueKind::Constant;
    Math::Vector3 constant{};
    Math::Vector3 min{};
    Math::Vector3 max{};
    std::string parameter;
    std::uint32_t wireNode = 0U;
    [[nodiscard]] bool operator==(const ParticleVectorInput&) const = default;
};

struct ParticleColorInput final {
    ParticleValueKind kind = ParticleValueKind::Constant;
    Math::Color constant{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    Math::Color minColor{1.0F, 1.0F, 1.0F};
    Math::Color maxColor{1.0F, 1.0F, 1.0F};
    ParticleGradient gradient;
    std::string parameter;
    [[nodiscard]] bool operator==(const ParticleColorInput&) const = default;
};

enum class ParticleModuleType : std::uint16_t {
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
struct ParticleModule final {
    std::uint32_t id = 0U; ///< Stable within the emitter (graph node id).
    ParticleModuleType type{};
    bool enabled = true;
    std::map<std::string, ParticleFloatInput> floats;
    std::map<std::string, ParticleVectorInput> vectors;
    std::map<std::string, ParticleColorInput> colors;
    std::map<std::string, std::int32_t> ints;   ///< Enums and counts.
    std::map<std::string, std::string> strings; ///< Asset paths (texture, mesh, material), emitter names.
    float graphX = 0.0F;
    float graphY = 0.0F;
    /// True when `(0, 0)` is an explicit graph position rather than "unplaced" (auto-layout).
    bool graphPosAtOrigin = false;
    [[nodiscard]] bool operator==(const ParticleModule&) const = default;
};

enum class ParticleSimSpace : std::uint8_t {
    World = 0,
    Local,
};

enum class ParticleSimTarget : std::uint8_t {
    Cpu = 0,
    Gpu,
    Auto,
};

struct ParticleMathNode final {
    std::uint32_t id = 0U;
    std::string op;
    std::vector<std::uint32_t> inputs;
    float constant = 0.0F;
    float graphX = 0.0F;
    float graphY = 0.0F;
    [[nodiscard]] bool operator==(const ParticleMathNode&) const = default;
};

/// Saved graph coordinates for one of the four fixed stage output nodes.
struct ParticleStageNodePos final {
    bool placed = false;
    float x = 0.0F;
    float y = 0.0F;
    [[nodiscard]] bool operator==(const ParticleStageNodePos&) const = default;
};

/// Preserved chain wire (`Out -> In`) between two detached modules so deleting one wire does not
/// erase remaining upstream links. Kept for authoring only and never compiled into runtime ops.
struct ParticleDetachedLink final {
    std::uint32_t fromModuleId = 0U;
    std::uint32_t toModuleId = 0U;
    [[nodiscard]] bool operator==(const ParticleDetachedLink&) const = default;
};

struct ParticleEmitterAsset final {
    std::string name = "Emitter";
    bool enabled = true;
    ParticleSimSpace space = ParticleSimSpace::World;
    ParticleSimTarget target = ParticleSimTarget::Cpu;
    std::uint32_t maxParticles = 1000U;
    std::uint32_t seed = 0U; ///< 0: derived from the instance seed.
    std::vector<ParticleModule> emitterUpdate;
    std::vector<ParticleModule> particleSpawn;
    std::vector<ParticleModule> particleUpdate;
    std::vector<ParticleModule> render;
    /// Modules that exist in the emitter graph but are not wired into their stage's chain. They are
    /// kept so authoring work survives save/load and graph rebuilds, but they are never compiled:
    /// only modules reachable through the stage chain contribute to the program.
    std::vector<ParticleModule> detached;
    std::vector<ParticleMathNode> mathNodes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> comments;
    /// Chain wires between modules in `detached`. Preserved for authoring and never compiled.
    std::vector<ParticleDetachedLink> detachedLinks;
    /// Saved positions for the 4 fixed stage output nodes, indexed by `ParticleStage`.
    std::array<ParticleStageNodePos, 4> stageNodePositions{};
    [[nodiscard]] bool operator==(const ParticleEmitterAsset&) const = default;
};

enum class ParticleUserParameterType : std::uint8_t {
    Float = 0,
    Vector,
    Color,
    Bool,
    Int,
};

struct ParticleUserParameter final {
    std::string name;
    ParticleUserParameterType type{};
    Math::Vector3 value{};
    float alpha = 1.0F;
    [[nodiscard]] bool operator==(const ParticleUserParameter&) const = default;
};

struct ParticleScalability final {
    float spawnScale = 1.0F;
    std::uint32_t maxParticlesCap = 0U;
    float cullDistance = 0.0F;
    [[nodiscard]] bool operator==(const ParticleScalability&) const = default;
};

struct ParticleSystemAsset final {
    std::vector<ParticleEmitterAsset> emitters;
    std::vector<ParticleUserParameter> parameters;
    float duration = 2.0F; ///< Seconds; one loop.
    bool looping = true;
    float warmUpSeconds = 0.0F;
    Math::Aabb fixedBounds{{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}}; ///< Culling; local space.
    std::array<ParticleScalability, 3> scalability{};                  ///< Low, Medium, High.
    [[nodiscard]] bool operator==(const ParticleSystemAsset&) const = default;
};

/// Which pipeline stage a module type belongs to.
enum class ParticleStage : std::uint8_t {
    EmitterUpdate = 0,
    ParticleSpawn,
    ParticleUpdate,
    Render,
};

/// Authoritative module metadata shared by the validator, compiler, serializer and editor.
struct ParticleModuleDescriptor final {
    ParticleModuleType type{};
    ParticleStage stage = ParticleStage::EmitterUpdate;
    std::string_view canonicalName;
    std::string_view displayName;
    std::string_view description;
    std::map<std::string, ParticleFloatInput> defaultFloats{};
    std::map<std::string, ParticleVectorInput> defaultVectors{};
    std::map<std::string, ParticleColorInput> defaultColors{};
    std::map<std::string, std::int32_t> defaultInts{};
    std::map<std::string, std::string> defaultStrings{};
};

/// Returns the catalog of all built-in particle modules (`vfx_module_catalog`).
[[nodiscard]] const std::vector<ParticleModuleDescriptor>& GetParticleModuleCatalog() noexcept;

/// Looks up metadata for `type`, or nullptr if invalid.
[[nodiscard]] const ParticleModuleDescriptor* FindParticleModuleDescriptor(
    ParticleModuleType type) noexcept;

/// Looks up metadata by serialized canonical type name (e.g. `"SpawnRate"`), or nullptr if unknown.
[[nodiscard]] const ParticleModuleDescriptor* FindParticleModuleDescriptorByName(
    std::string_view canonicalName) noexcept;

/// Constructs a module with its default identity (`id`, `type`, position) and empty override maps;
/// omitted input names fall back to the catalog's defaults.
[[nodiscard]] ParticleModule MakeDefaultParticleModule(
    std::uint32_t id, ParticleModuleType type, float graphX = 0.0F, float graphY = 0.0F);

/// Returns true iff `system` satisfies every structural, range, name, parameter and catalog rule.
[[nodiscard]] bool IsParticleSystemAssetValid(const ParticleSystemAsset& system) noexcept;

/// Saves `system` to `path` as a `.uvfx` envelope (`AssetKind::ParticleSystem`), omitting inputs
/// that match their module default.
[[nodiscard]] bool SaveParticleSystemAsset(const ParticleSystemAsset& system,
                                              const std::filesystem::path& path);

/// Loads `path` as a `.uvfx` envelope (`AssetKind::ParticleSystem`), validating schema and contents
/// before modifying `outSystem`.
[[nodiscard]] bool LoadParticleSystemAsset(const std::filesystem::path& path,
                                              ParticleSystemAsset& outSystem);

} // namespace UVE::Asset
