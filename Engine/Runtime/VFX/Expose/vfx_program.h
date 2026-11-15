// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "particle_system_asset.h"
#include "vfx_values.h"

namespace UVE::VFX {

/// Bitmask indicating which per-particle SoA attribute arrays a compiled program reads or writes.
enum class VfxAttributeBitUVE : std::uint32_t {
    None = 0U,
    Position = 1U << 0U,
    Velocity = 1U << 1U,
    Age = 1U << 2U,
    Lifetime = 1U << 3U,
    Color = 1U << 4U,
    Size = 1U << 5U,
    Rotation = 1U << 6U,      ///< Stored in degrees.
    Spin = 1U << 7U,          ///< Stored in degrees per second.
    Frame = 1U << 8U,
    Mass = 1U << 9U,
    Id = 1U << 10U,
    Seed = 1U << 11U,
    PrevPosition = 1U << 12U,
    SpawnColor = 1U << 13U,
    SpawnSize = 1U << 14U,
    SpawnOrigin = 1U << 15U,
};

using VfxAttributeMaskUVE = std::uint32_t;

[[nodiscard]] constexpr VfxAttributeMaskUVE ToMaskUVE(const VfxAttributeBitUVE bit) noexcept {
    return static_cast<VfxAttributeMaskUVE>(bit);
}

[[nodiscard]] constexpr bool HasVfxAttributeUVE(const VfxAttributeMaskUVE mask,
                                                const VfxAttributeBitUVE bit) noexcept {
    return (mask & ToMaskUVE(bit)) != 0U;
}

/// Compiler diagnostic produced while validating and lowering an emitter graph.
struct ParticleCompileMessageUVE final {
    enum class Severity : std::uint8_t {
        Error = 0,
        Warning,
    };

    Severity severity = Severity::Error;
    std::uint32_t moduleId = 0U;
    std::string text;
};

struct VfxEmitterUpdateOpUVE final {
    Asset::ParticleModuleTypeUVE type = Asset::ParticleModuleTypeUVE::SpawnRate;
    std::uint32_t moduleId = 0U;
    VfxCompiledFloatInputUVE rate;
    VfxCompiledFloatInputUVE time;
    VfxCompiledFloatInputUVE interval;
    VfxCompiledFloatInputUVE perUnit;
    std::int32_t count = 10;
    std::int32_t cycles = 1;
};

struct VfxParticleSpawnOpUVE final {
    Asset::ParticleModuleTypeUVE type = Asset::ParticleModuleTypeUVE::ShapePoint;
    std::uint32_t moduleId = 0U;
    VfxCompiledFloatInputUVE floatA;
    VfxCompiledFloatInputUVE floatB;
    VfxCompiledFloatInputUVE floatC;
    VfxCompiledFloatInputUVE normalSpeed;
    VfxCompiledVectorInputUVE vecA;
    VfxCompiledColorInputUVE colorA;
    std::int32_t surface = 0;
    std::int32_t hemisphere = 0;
    std::string assetPath;
};

struct VfxParticleUpdateOpUVE final {
    Asset::ParticleModuleTypeUVE type = Asset::ParticleModuleTypeUVE::Gravity;
    std::uint32_t moduleId = 0U;
    VfxCompiledFloatInputUVE floatA;
    VfxCompiledFloatInputUVE floatB;
    VfxCompiledFloatInputUVE floatC;
    VfxCompiledFloatInputUVE floatD;
    VfxCompiledVectorInputUVE vecA;
    VfxCompiledVectorInputUVE vecB;
    VfxCompiledColorInputUVE colorA;
    std::int32_t intA = 0;
    std::int32_t intB = 0;
    std::int32_t intC = 0;
    std::uint32_t targetEmitterIndex = std::numeric_limits<std::uint32_t>::max();
};

struct VfxRendererDescUVE final {
    Asset::ParticleModuleTypeUVE type = Asset::ParticleModuleTypeUVE::SpriteRenderer;
    std::uint32_t moduleId = 0U;
    std::int32_t facing = 0;
    std::int32_t blend = 0;
    std::int32_t rows = 1;
    std::int32_t cols = 1;
    std::int32_t frameBlend = 0;
    VfxCompiledFloatInputUVE stretchScale;
    VfxCompiledFloatInputUVE alphaCutoff;
    VfxCompiledFloatInputUVE softDistance;
    VfxCompiledFloatInputUVE cameraFadeNear;
    VfxCompiledFloatInputUVE cameraFadeFar;
    VfxCompiledFloatInputUVE emissive;
    VfxCompiledVectorInputUVE axis;

    std::int32_t perParticle = 0;
    std::int32_t trailLength = 16;
    std::int32_t ribbonMode = 0;
    VfxCompiledFloatInputUVE width;
    VfxCompiledFloatInputUVE tileLength;

    std::int32_t orient = 0;

    VfxCompiledFloatInputUVE radius;
    VfxCompiledFloatInputUVE intensity;

    std::string texturePath;
    std::string meshPath;
    std::string materialPath;
};

/// Flat, self-contained compiled program executed for one emitter by the CPU or GPU simulator.
struct ParticleProgramUVE final {
    std::string emitterName;
    bool enabled = true;
    Asset::ParticleSimSpaceUVE space = Asset::ParticleSimSpaceUVE::World;
    Asset::ParticleSimTargetUVE target = Asset::ParticleSimTargetUVE::Cpu;
    std::uint32_t maxParticles = 1000U;
    std::uint32_t seed = 0U;
    VfxAttributeMaskUVE attributeMask = 0U;

    std::vector<VfxEmitterUpdateOpUVE> emitterUpdateOps;
    std::vector<VfxParticleSpawnOpUVE> particleSpawnOps;
    std::vector<VfxParticleUpdateOpUVE> particleUpdateOps;
    std::vector<VfxRendererDescUVE> renderers;

    std::vector<VfxCurveTableUVE> curveTables;
    std::vector<VfxGradientTableUVE> gradientTables;
};

/// Optional external filesystem context used to resolve relative `.uvtex`, `.uvmodel` and material paths.
struct VfxCompileContextUVE final {
    std::filesystem::path projectRoot;
};

/// Compiles `system.emitters[emitterIndex]` into `outProgram`. On any error, returns false without
/// overwriting `outProgram`, so callers can continue running the previous good program.
[[nodiscard]] bool CompileParticleEmitterUVE(const Asset::ParticleSystemAssetUVE& system,
                                             std::size_t emitterIndex,
                                             ParticleProgramUVE& outProgram,
                                             std::vector<ParticleCompileMessageUVE>& messages);

/// Overload with an explicit project root for resolving referenced assets.
[[nodiscard]] bool CompileParticleEmitterUVE(const Asset::ParticleSystemAssetUVE& system,
                                             std::size_t emitterIndex,
                                             const VfxCompileContextUVE& context,
                                             ParticleProgramUVE& outProgram,
                                             std::vector<ParticleCompileMessageUVE>& messages);

} // namespace UVE::VFX
