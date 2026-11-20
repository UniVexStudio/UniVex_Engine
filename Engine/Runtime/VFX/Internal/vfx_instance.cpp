// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_instance.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "math_random.h"
#include "scalar.h"
#include "vfx_shapes.h"

namespace UVE::VFX {
namespace {

constexpr float kMaxFrameDtUVE = 0.1F;
constexpr float kMaxSubStepDtUVE = 1.0F / 30.0F;
constexpr float kWarmUpStepDtUVE = 1.0F / 30.0F;

[[nodiscard]] VfxParameterValueUVE ConvertParameterUVE(const Asset::ParticleUserParameterUVE& param) noexcept {
    VfxParameterValueUVE out;
    out.type = param.type;
    out.vector = param.value;
    out.alpha = param.alpha;
    return out;
}

[[nodiscard]] std::uint32_t EffectiveSeedUVE(const ParticleProgramUVE& program,
                                             const std::uint32_t instanceSeed) noexcept {
    return program.seed != 0U ? program.seed : instanceSeed;
}

[[nodiscard]] Math::Vector3UVE LerpVec3UVE(const Math::Vector3UVE& a,
                                           const Math::Vector3UVE& b,
                                           const float t) noexcept {
    return Math::Vector3UVE{
        Math::LerpUVE(a.x, b.x, t),
        Math::LerpUVE(a.y, b.y, t),
        Math::LerpUVE(a.z, b.z, t),
    };
}

[[nodiscard]] Math::Vector3UVE SampleVelocityInConeUVE(Math::RandomUVE& rng,
                                                     const Math::Vector3UVE& axisInput,
                                                     const float angleDegrees,
                                                     const float speed) noexcept {
    Math::Vector3UVE w{0.0F, 1.0F, 0.0F};
    if (Math::LengthSquaredUVE(axisInput) > 1e-12F) {
        w = Math::NormalizeUVE(axisInput);
    }
    const Math::Vector3UVE ref = std::abs(w.y) < 0.99F ? Math::Vector3UVE{0.0F, 1.0F, 0.0F}
                                                       : Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE uBasis = Math::NormalizeUVE(Math::CrossUVE(ref, w));
    const Math::Vector3UVE vBasis = Math::CrossUVE(w, uBasis);

    const float clampedAngle = std::clamp(angleDegrees, 0.0F, 180.0F);
    const float cosMax = std::cos(Math::DegToRadUVE(clampedAngle));
    const float u = rng.NextFloatUVE();
    const float v = rng.NextFloatUVE();
    const float cosTheta = Math::LerpUVE(1.0F, cosMax, u);
    const float sinTheta = std::sqrt(std::max(0.0F, 1.0F - cosTheta * cosTheta));
    const float phi = 2.0F * Math::kPiUVE * v;
    const Math::Vector3UVE dir =
        w * cosTheta + (uBasis * std::cos(phi) + vBasis * std::sin(phi)) * sinTheta;
    return dir * speed;
}

void ResetEmitterStateArraysUVE(const ParticleProgramUVE& program,
                                VfxEmitterStateUVE& state,
                                const VfxOwnerUVE& initialOwner) {
    state.liveCount = 0U;
    state.nextId = 0U;
    state.emitterAge = 0.0F;
    state.completed = false;
    state.hasLastOwnerPos = true;
    state.lastOwnerPos = initialOwner.position;
    state.spawnCarry.assign(program.emitterUpdateOps.size(), 0.0);
    state.stepCounter = 0U;

    state.posX.clear();
    state.posY.clear();
    state.posZ.clear();
    state.velX.clear();
    state.velY.clear();
    state.velZ.clear();
    state.age.clear();
    state.lifetime.clear();
    state.colR.clear();
    state.colG.clear();
    state.colB.clear();
    state.colA.clear();
    state.spawnColR.clear();
    state.spawnColG.clear();
    state.spawnColB.clear();
    state.spawnColA.clear();
    state.sizeX.clear();
    state.sizeY.clear();
    state.spawnSizeX.clear();
    state.spawnSizeY.clear();
    state.rotation.clear();
    state.spin.clear();
    state.frame.clear();
    state.mass.clear();
    state.prevX.clear();
    state.prevY.clear();
    state.prevZ.clear();
    state.spawnX.clear();
    state.spawnY.clear();
    state.spawnZ.clear();
    state.id.clear();
    state.seed.clear();
    state.accX.clear();
    state.accY.clear();
    state.accZ.clear();
    state.normalizedAge.clear();

    state.liveBounds = Math::AabbUVE{};
    state.hasLiveBounds = false;
}

void SwapRemoveParticleAtUVE(VfxEmitterStateUVE& state,
                             const VfxAttributeMaskUVE mask,
                             const std::size_t index) {
    const std::size_t last = state.liveCount - 1U;
    auto removeFrom = [index, last](auto& vec) {
        if (index < last) {
            vec[index] = vec[last];
        }
        vec.pop_back();
    };

    removeFrom(state.id);
    removeFrom(state.seed);
    removeFrom(state.age);
    removeFrom(state.lifetime);

    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Position)) {
        removeFrom(state.posX);
        removeFrom(state.posY);
        removeFrom(state.posZ);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Velocity)) {
        removeFrom(state.velX);
        removeFrom(state.velY);
        removeFrom(state.velZ);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Color)) {
        removeFrom(state.colR);
        removeFrom(state.colG);
        removeFrom(state.colB);
        removeFrom(state.colA);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnColor)) {
        removeFrom(state.spawnColR);
        removeFrom(state.spawnColG);
        removeFrom(state.spawnColB);
        removeFrom(state.spawnColA);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Size)) {
        removeFrom(state.sizeX);
        removeFrom(state.sizeY);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnSize)) {
        removeFrom(state.spawnSizeX);
        removeFrom(state.spawnSizeY);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Rotation)) {
        removeFrom(state.rotation);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Spin)) {
        removeFrom(state.spin);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Frame)) {
        removeFrom(state.frame);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Mass)) {
        removeFrom(state.mass);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::PrevPosition)) {
        removeFrom(state.prevX);
        removeFrom(state.prevY);
        removeFrom(state.prevZ);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnOrigin)) {
        removeFrom(state.spawnX);
        removeFrom(state.spawnY);
        removeFrom(state.spawnZ);
    }

    --state.liveCount;
}

void PushSpawnedParticleUVE(const std::size_t emitterIndex,
                            const ParticleProgramUVE& program,
                            VfxEmitterStateUVE& state,
                            const std::uint32_t instanceSeed,
                            const Math::Vector3UVE& spawnOriginPos,
                            const VfxOwnerUVE& owner,
                            const std::vector<VfxParameterValueUVE>& parameters) {
    const std::uint32_t particleId = state.nextId++;
    const std::uint32_t particleSeed = HashVfxSeedUVE(EffectiveSeedUVE(program, instanceSeed), particleId);
    Math::RandomUVE rng(particleSeed, static_cast<std::uint64_t>(emitterIndex));

    Math::Vector3UVE localPos{};
    Math::Vector3UVE localVel{};
    float inheritOwnerVelScale = 0.0F;
    float particleLifetime = 1.0F;
    VfxLinearColorAlphaUVE color{{1.0F, 1.0F, 1.0F}, 1.0F};
    float sizeX = 1.0F;
    float sizeY = 1.0F;
    float rotationDeg = 0.0F;
    float spinDeg = 0.0F;
    float spriteFrame = 0.0F;
    float particleMass = 1.0F;

    for (const VfxParticleSpawnOpUVE& op : program.particleSpawnOps) {
        switch (op.type) {
            case Asset::ParticleModuleTypeUVE::ShapePoint: {
                const VfxShapeSampleUVE sample = SampleShapePointUVE(rng);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInputUVE(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleTypeUVE::ShapeSphere: {
                const float radius = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const VfxShapeSampleUVE sample = SampleShapeSphereUVE(rng, radius, op.surface != 0, op.hemisphere != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInputUVE(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleTypeUVE::ShapeBox: {
                const Math::Vector3UVE size = EvaluateCompiledVectorInputUVE(op.vecA, &rng, parameters);
                const VfxShapeSampleUVE sample = SampleShapeBoxUVE(rng, size, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInputUVE(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleTypeUVE::ShapeCone: {
                const float angle = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                                  program.curveTables, parameters);
                const float length = EvaluateCompiledFloatInputUVE(op.floatB, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const float radius = EvaluateCompiledFloatInputUVE(op.floatC, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const VfxShapeSampleUVE sample = SampleShapeConeUVE(rng, angle, length, radius, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInputUVE(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleTypeUVE::ShapeCylinder: {
                const float radius = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const float height = EvaluateCompiledFloatInputUVE(op.floatB, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const VfxShapeSampleUVE sample = SampleShapeCylinderUVE(rng, radius, height, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInputUVE(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleTypeUVE::ShapeTorus: {
                const float majorRadius = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                                        program.curveTables, parameters);
                const float minorRadius = EvaluateCompiledFloatInputUVE(op.floatB, 0.0F, &rng,
                                                                        program.curveTables, parameters);
                const VfxShapeSampleUVE sample = SampleShapeTorusUVE(rng, majorRadius, minorRadius, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInputUVE(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleTypeUVE::InitialVelocity:
                localVel = EvaluateCompiledVectorInputUVE(op.vecA, &rng, parameters);
                break;
            case Asset::ParticleModuleTypeUVE::VelocityInCone: {
                const Math::Vector3UVE axis = EvaluateCompiledVectorInputUVE(op.vecA, &rng, parameters);
                const float angle = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                                  program.curveTables, parameters);
                const float speed = EvaluateCompiledFloatInputUVE(op.floatB, 0.0F, &rng,
                                                                  program.curveTables, parameters);
                localVel = SampleVelocityInConeUVE(rng, axis, angle, speed);
                break;
            }
            case Asset::ParticleModuleTypeUVE::InitialLifetime:
                particleLifetime = std::max(
                    0.0F, EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng, program.curveTables, parameters));
                break;
            case Asset::ParticleModuleTypeUVE::InitialColor:
                color = EvaluateCompiledColorInputUVE(op.colorA, 0.0F, &rng, program.gradientTables, parameters);
                break;
            case Asset::ParticleModuleTypeUVE::InitialSize: {
                const float sFloat = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const Math::Vector3UVE sVec = EvaluateCompiledVectorInputUVE(op.vecA, &rng, parameters);
                sizeX = sFloat * sVec.x;
                sizeY = sFloat * sVec.y;
                break;
            }
            case Asset::ParticleModuleTypeUVE::InitialRotation:
                rotationDeg = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                            program.curveTables, parameters);
                break;
            case Asset::ParticleModuleTypeUVE::InitialSpin:
                spinDeg = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                        program.curveTables, parameters);
                break;
            case Asset::ParticleModuleTypeUVE::InitialSpriteFrame:
                spriteFrame = EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                            program.curveTables, parameters);
                break;
            case Asset::ParticleModuleTypeUVE::InitialMass:
                particleMass = std::max(
                    1e-4F, EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng, program.curveTables, parameters));
                break;
            case Asset::ParticleModuleTypeUVE::InheritOwnerVelocity:
                inheritOwnerVelScale += EvaluateCompiledFloatInputUVE(op.floatA, 0.0F, &rng,
                                                                      program.curveTables, parameters);
                break;
            default:
                break;
        }
    }

    Math::Vector3UVE finalPos = localPos;
    Math::Vector3UVE finalVel = localVel;
    if (program.space == Asset::ParticleSimSpaceUVE::World) {
        finalPos = spawnOriginPos + Math::RotateVectorUVE(owner.rotation, localPos);
        finalVel = Math::RotateVectorUVE(owner.rotation, localVel) + owner.velocity * inheritOwnerVelScale;
    } else if (inheritOwnerVelScale != 0.0F) {
        finalVel += Math::RotateVectorUVE(Math::ConjugateUVE(owner.rotation), owner.velocity) *
                    inheritOwnerVelScale;
    }

    const VfxAttributeMaskUVE mask = program.attributeMask;
    state.id.push_back(particleId);
    state.seed.push_back(particleSeed);
    state.age.push_back(0.0F);
    state.lifetime.push_back(particleLifetime);

    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Position)) {
        state.posX.push_back(finalPos.x);
        state.posY.push_back(finalPos.y);
        state.posZ.push_back(finalPos.z);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Velocity)) {
        state.velX.push_back(finalVel.x);
        state.velY.push_back(finalVel.y);
        state.velZ.push_back(finalVel.z);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Color)) {
        state.colR.push_back(color.color.r);
        state.colG.push_back(color.color.g);
        state.colB.push_back(color.color.b);
        state.colA.push_back(color.alpha);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnColor)) {
        state.spawnColR.push_back(color.color.r);
        state.spawnColG.push_back(color.color.g);
        state.spawnColB.push_back(color.color.b);
        state.spawnColA.push_back(color.alpha);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Size)) {
        state.sizeX.push_back(sizeX);
        state.sizeY.push_back(sizeY);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnSize)) {
        state.spawnSizeX.push_back(sizeX);
        state.spawnSizeY.push_back(sizeY);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Rotation)) {
        state.rotation.push_back(rotationDeg);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Spin)) {
        state.spin.push_back(spinDeg);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Frame)) {
        state.frame.push_back(spriteFrame);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Mass)) {
        state.mass.push_back(particleMass);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::PrevPosition)) {
        state.prevX.push_back(finalPos.x);
        state.prevY.push_back(finalPos.y);
        state.prevZ.push_back(finalPos.z);
    }
    if (HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnOrigin)) {
        state.spawnX.push_back(finalPos.x);
        state.spawnY.push_back(finalPos.y);
        state.spawnZ.push_back(finalPos.z);
    }

    ++state.liveCount;
}

[[nodiscard]] std::uint64_t EvaluateBurstCrossingsUVE(const VfxEmitterUpdateOpUVE& op,
                                                      const ParticleProgramUVE& program,
                                                      const std::vector<VfxParameterValueUVE>& parameters,
                                                      const float sliceStart,
                                                      const float sliceEnd,
                                                      const float sliceNormAge,
                                                      const float effectiveSpawnScale) noexcept {
    const std::int32_t rawCount = op.count;
    if (rawCount <= 0 || effectiveSpawnScale <= 0.0F || sliceEnd <= sliceStart) {
        return 0ULL;
    }

    const std::uint32_t scaledCount = effectiveSpawnScale == 1.0F
        ? static_cast<std::uint32_t>(rawCount)
        : static_cast<std::uint32_t>(std::max(0.0F, std::floor(static_cast<float>(rawCount) * effectiveSpawnScale)));
    if (scaledCount == 0U) {
        return 0ULL;
    }

    const float time = std::max(
        0.0F, EvaluateCompiledFloatInputUVE(op.time, sliceNormAge, nullptr, program.curveTables, parameters));
    const float interval = std::max(
        0.0F, EvaluateCompiledFloatInputUVE(op.interval, sliceNormAge, nullptr, program.curveTables, parameters));

    if (interval <= 1e-6F) {
        if (time >= sliceStart && time < sliceEnd) {
            const std::uint32_t repeatCount = op.cycles <= 1 ? 1U : static_cast<std::uint32_t>(op.cycles);
            return static_cast<std::uint64_t>(scaledCount) * static_cast<std::uint64_t>(repeatCount);
        }
        return 0ULL;
    }

    const std::int64_t maxK = op.cycles > 0 ? static_cast<std::int64_t>(op.cycles - 1) : 100000LL;
    const double dStart =
        std::floor((static_cast<double>(sliceStart) - static_cast<double>(time)) / static_cast<double>(interval)) -
        1.0;
    const double dEnd =
        std::ceil((static_cast<double>(sliceEnd) - static_cast<double>(time)) / static_cast<double>(interval)) + 1.0;
    const std::int64_t kStart = std::max<std::int64_t>(0, static_cast<std::int64_t>(dStart));
    const std::int64_t kEnd = std::min<std::int64_t>(maxK, static_cast<std::int64_t>(dEnd));

    std::uint64_t firings = 0ULL;
    for (std::int64_t k = kStart; k <= kEnd; ++k) {
        const float burstTime = time + static_cast<float>(k) * interval;
        if (burstTime >= sliceStart && burstTime < sliceEnd) {
            ++firings;
        }
    }
    return firings * static_cast<std::uint64_t>(scaledCount);
}

void RunKillPassUVE(const ParticleProgramUVE& program,
                    VfxEmitterStateUVE& state,
                    const std::vector<VfxParameterValueUVE>& parameters) {
    if (state.liveCount == 0U) {
        return;
    }

    const bool hasPosition = HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Position);
    std::vector<const VfxParticleUpdateOpUVE*> killBoundsOps;
    for (const VfxParticleUpdateOpUVE& op : program.particleUpdateOps) {
        if (op.type == Asset::ParticleModuleTypeUVE::KillByBounds && hasPosition) {
            killBoundsOps.push_back(&op);
        }
    }

    for (std::size_t reverseIdx = state.liveCount; reverseIdx > 0U; --reverseIdx) {
        const std::size_t i = reverseIdx - 1U;
        bool shouldKill = !(state.age[i] < state.lifetime[i]);

        if (!shouldKill && !killBoundsOps.empty()) {
            const float px = state.posX[i];
            const float py = state.posY[i];
            const float pz = state.posZ[i];
            for (const VfxParticleUpdateOpUVE* op : killBoundsOps) {
                const Math::Vector3UVE minV = EvaluateCompiledVectorInputUVE(op->vecA, nullptr, parameters);
                const Math::Vector3UVE maxV = EvaluateCompiledVectorInputUVE(op->vecB, nullptr, parameters);
                const bool inside = (px >= minV.x && px <= maxV.x) &&
                                    (py >= minV.y && py <= maxV.y) &&
                                    (pz >= minV.z && pz <= maxV.z);
                if (op->intA != 0 ? inside : !inside) {
                    shouldKill = true;
                    break;
                }
            }
        }

        if (shouldKill) {
            SwapRemoveParticleAtUVE(state, program.attributeMask, i);
        }
    }
}

void UpdateLiveBoundsUVE(const ParticleProgramUVE& program,
                         VfxEmitterStateUVE& state,
                         const VfxOwnerUVE& owner) {
    if (state.liveCount == 0U) {
        state.liveBounds = Math::AabbUVE{};
        state.hasLiveBounds = false;
        return;
    }

    if (!HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Position)) {
        const Math::Vector3UVE origin =
            program.space == Asset::ParticleSimSpaceUVE::World ? owner.position : Math::Vector3UVE{};
        state.liveBounds = Math::AabbUVE{origin, origin};
        state.hasLiveBounds = true;
        return;
    }

    float minX = state.posX[0];
    float minY = state.posY[0];
    float minZ = state.posZ[0];
    float maxX = minX;
    float maxY = minY;
    float maxZ = minZ;

    for (std::size_t i = 1U; i < state.liveCount; ++i) {
        minX = std::min(minX, state.posX[i]);
        minY = std::min(minY, state.posY[i]);
        minZ = std::min(minZ, state.posZ[i]);
        maxX = std::max(maxX, state.posX[i]);
        maxY = std::max(maxY, state.posY[i]);
        maxZ = std::max(maxZ, state.posZ[i]);
    }
    state.liveBounds = Math::AabbUVE{
        Math::Vector3UVE{minX, minY, minZ},
        Math::Vector3UVE{maxX, maxY, maxZ},
    };
    state.hasLiveBounds = true;
}

} // namespace

bool VfxInstanceUVE::InitializeUVE(const Asset::ParticleSystemAssetUVE& system,
                                   const VfxOwnerUVE& initialOwner,
                                   const std::uint32_t instanceSeed,
                                   const VfxCompileContextUVE& compileContext,
                                   std::vector<ParticleCompileMessageUVE>* const outMessages) {
    if (!Asset::IsParticleSystemAssetValidUVE(system)) {
        if (outMessages != nullptr) {
            outMessages->push_back(ParticleCompileMessageUVE{
                ParticleCompileMessageUVE::Severity::Error,
                0U,
                "Particle system asset failed structural validation.",
            });
        }
        return false;
    }

    std::vector<EmitterRuntimeUVE> compiledEmitters;
    compiledEmitters.resize(system.emitters.size());
    if (outMessages != nullptr) {
        outMessages->clear();
    }

    for (std::size_t idx = 0U; idx < system.emitters.size(); ++idx) {
        std::vector<ParticleCompileMessageUVE> emitterMessages;
        const bool ok = CompileParticleEmitterUVE(system, idx, compileContext, compiledEmitters[idx].program,
                                                  emitterMessages);
        if (outMessages != nullptr) {
            outMessages->insert(outMessages->end(), emitterMessages.begin(), emitterMessages.end());
        }
        if (!ok) {
            return false;
        }
    }

    m_emitters = std::move(compiledEmitters);
    m_instanceSeed = instanceSeed != 0U ? instanceSeed : 1U;
    m_duration = system.duration;
    m_looping = system.looping;
    m_warmUpSeconds = system.warmUpSeconds;
    m_fixedBounds = system.fixedBounds;
    m_scalability = system.scalability;

    m_defaultParameters.clear();
    m_parameterIndices.clear();
    m_defaultParameters.reserve(system.parameters.size());
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(system.parameters.size()); ++idx) {
        m_defaultParameters.push_back(ConvertParameterUVE(system.parameters[idx]));
        m_parameterIndices.emplace(system.parameters[idx].name, idx);
    }
    m_parameters = m_defaultParameters;
    m_initialized = true;

    ResetUVE(initialOwner);
    return true;
}

void VfxInstanceUVE::ResetUVE(const VfxOwnerUVE& initialOwner) {
    m_stopped = false;
    m_killed = false;
    m_lastOwnerPos = initialOwner.position;
    for (EmitterRuntimeUVE& emitter : m_emitters) {
        ResetEmitterStateArraysUVE(emitter.program, emitter.state, initialOwner);
    }

    if (m_initialized && m_warmUpSeconds > 0.0F) {
        float remaining = m_warmUpSeconds;
        while (remaining > 1e-6F) {
            const float step = std::min(remaining, kWarmUpStepDtUVE);
            StepSingleSubstepUVE(step, initialOwner.position, initialOwner, 1.0F, {});
            remaining -= step;
        }
    }
}

void VfxInstanceUVE::StopUVE() noexcept {
    m_stopped = true;
}

void VfxInstanceUVE::KillUVE() noexcept {
    m_stopped = true;
    m_killed = true;
    for (EmitterRuntimeUVE& emitter : m_emitters) {
        emitter.state.completed = true;
        emitter.state.liveCount = 0U;
        emitter.state.posX.clear();
        emitter.state.posY.clear();
        emitter.state.posZ.clear();
        emitter.state.velX.clear();
        emitter.state.velY.clear();
        emitter.state.velZ.clear();
        emitter.state.age.clear();
        emitter.state.lifetime.clear();
        emitter.state.colR.clear();
        emitter.state.colG.clear();
        emitter.state.colB.clear();
        emitter.state.colA.clear();
        emitter.state.spawnColR.clear();
        emitter.state.spawnColG.clear();
        emitter.state.spawnColB.clear();
        emitter.state.spawnColA.clear();
        emitter.state.sizeX.clear();
        emitter.state.sizeY.clear();
        emitter.state.spawnSizeX.clear();
        emitter.state.spawnSizeY.clear();
        emitter.state.rotation.clear();
        emitter.state.spin.clear();
        emitter.state.frame.clear();
        emitter.state.mass.clear();
        emitter.state.prevX.clear();
        emitter.state.prevY.clear();
        emitter.state.prevZ.clear();
        emitter.state.spawnX.clear();
        emitter.state.spawnY.clear();
        emitter.state.spawnZ.clear();
        emitter.state.id.clear();
        emitter.state.seed.clear();
        emitter.state.liveBounds = Math::AabbUVE{};
        emitter.state.hasLiveBounds = false;
    }
}

void VfxInstanceUVE::TickUVE(const float dt,
                             const VfxOwnerUVE& owner,
                             const float externalSpawnScale,
                             const std::span<const VfxColliderVolumeUVE> sceneColliders) {
    if (!m_initialized || m_killed) {
        return;
    }
    const float clampedDt = std::clamp(dt, 0.0F, kMaxFrameDtUVE);
    if (clampedDt <= 0.0F) {
        return;
    }

    std::uint32_t substeps = 1U;
    if (clampedDt > kMaxSubStepDtUVE + 1e-6F) {
        substeps = static_cast<std::uint32_t>(std::ceil(clampedDt * 30.0F));
        while (clampedDt / static_cast<float>(substeps) > kMaxSubStepDtUVE + 1e-6F) {
            ++substeps;
        }
    }

    const float subDt = clampedDt / static_cast<float>(substeps);
    const Math::Vector3UVE frameStartPos = m_lastOwnerPos;

    for (std::uint32_t step = 0U; step < substeps; ++step) {
        const float startFrac = static_cast<float>(step) / static_cast<float>(substeps);
        const float endFrac = static_cast<float>(step + 1U) / static_cast<float>(substeps);
        VfxOwnerUVE stepOwner = owner;
        stepOwner.position = LerpVec3UVE(frameStartPos, owner.position, endFrac);
        const Math::Vector3UVE prevStepPos = LerpVec3UVE(frameStartPos, owner.position, startFrac);
        StepSingleSubstepUVE(subDt, prevStepPos, stepOwner, externalSpawnScale, sceneColliders);
    }
    m_lastOwnerPos = owner.position;
}

bool VfxInstanceUVE::SetParameterFloatUVE(const std::string_view name, const float value) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterTypeUVE::Float) {
        return false;
    }
    m_parameters[it->second].vector.x = value;
    return true;
}

bool VfxInstanceUVE::SetParameterVectorUVE(const std::string_view name,
                                           const Math::Vector3UVE& value) noexcept {
    if (!Math::IsFiniteUVE(value)) {
        return false;
    }
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterTypeUVE::Vector) {
        return false;
    }
    m_parameters[it->second].vector = value;
    return true;
}

bool VfxInstanceUVE::SetParameterColorUVE(const std::string_view name,
                                          const Math::ColorUVE& color,
                                          const float alpha) noexcept {
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
        !std::isfinite(alpha)) {
        return false;
    }
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterTypeUVE::Color) {
        return false;
    }
    m_parameters[it->second].vector = Math::Vector3UVE{color.r, color.g, color.b};
    m_parameters[it->second].alpha = alpha;
    return true;
}

bool VfxInstanceUVE::SetParameterBoolUVE(const std::string_view name, const bool value) noexcept {
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterTypeUVE::Bool) {
        return false;
    }
    m_parameters[it->second].vector.x = value ? 1.0F : 0.0F;
    return true;
}

bool VfxInstanceUVE::SetParameterIntUVE(const std::string_view name, const std::int32_t value) noexcept {
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterTypeUVE::Int) {
        return false;
    }
    m_parameters[it->second].vector.x = static_cast<float>(value);
    return true;
}

void VfxInstanceUVE::ClearParameterOverridesUVE() noexcept {
    m_parameters = m_defaultParameters;
}

bool VfxInstanceUVE::IsAliveUVE() const noexcept {
    if (!m_initialized || m_killed) {
        return false;
    }
    for (const EmitterRuntimeUVE& emitter : m_emitters) {
        if (!emitter.program.enabled) {
            continue;
        }
        if (emitter.state.liveCount > 0U) {
            return true;
        }
        if (!m_stopped && !emitter.state.completed) {
            return true;
        }
    }
    return false;
}

std::size_t VfxInstanceUVE::GetLiveCountUVE() const noexcept {
    std::size_t total = 0U;
    for (const EmitterRuntimeUVE& emitter : m_emitters) {
        total += emitter.state.liveCount;
    }
    return total;
}

const ParticleProgramUVE& VfxInstanceUVE::GetProgramUVE(const std::size_t emitterIndex) const {
    if (emitterIndex >= m_emitters.size()) {
        throw std::out_of_range("VfxInstanceUVE emitter index out of range");
    }
    return m_emitters[emitterIndex].program;
}

const VfxEmitterStateUVE& VfxInstanceUVE::GetEmitterStateUVE(const std::size_t emitterIndex) const {
    if (emitterIndex >= m_emitters.size()) {
        throw std::out_of_range("VfxInstanceUVE emitter index out of range");
    }
    return m_emitters[emitterIndex].state;
}

Math::AabbUVE VfxInstanceUVE::GetLiveBoundsUVE() const noexcept {
    bool initialized = false;
    Math::AabbUVE combined{};
    for (const EmitterRuntimeUVE& emitter : m_emitters) {
        if (!emitter.state.hasLiveBounds) {
            continue;
        }
        combined = initialized ? combined.UnionUVE(emitter.state.liveBounds) : emitter.state.liveBounds;
        initialized = true;
    }
    return combined;
}

void VfxInstanceUVE::StepSingleSubstepUVE(const float dt,
                                          const Math::Vector3UVE& prevOwnerPos,
                                          const VfxOwnerUVE& owner,
                                          const float externalSpawnScale,
                                          const std::span<const VfxColliderVolumeUVE> sceneColliders) {
    for (std::size_t idx = 0U; idx < m_emitters.size(); ++idx) {
        StepEmitterUVE(idx, m_emitters[idx], dt, prevOwnerPos, owner, externalSpawnScale, sceneColliders);
    }
}

void VfxInstanceUVE::StepEmitterUVE(const std::size_t emitterIndex,
                                    EmitterRuntimeUVE& runtime,
                                    const float dt,
                                    const Math::Vector3UVE& prevOwnerPos,
                                    const VfxOwnerUVE& owner,
                                    const float externalSpawnScale,
                                    const std::span<const VfxColliderVolumeUVE> sceneColliders) {
    const ParticleProgramUVE& program = runtime.program;
    VfxEmitterStateUVE& state = runtime.state;
    if (!program.enabled) {
        return;
    }

    const Math::Vector3UVE prevPos = state.hasLastOwnerPos ? prevOwnerPos : owner.position;
    state.lastOwnerPos = owner.position;
    state.hasLastOwnerPos = true;

    const std::uint8_t quality = std::min<std::uint8_t>(owner.qualityLevel, 2U);
    const Asset::ParticleScalabilityUVE& tier = m_scalability[quality];
    const float effectiveSpawnScale = std::max(0.0F, tier.spawnScale) * std::max(0.0F, externalSpawnScale);
    std::uint32_t maxCapacity = program.maxParticles;
    if (tier.maxParticlesCap > 0U) {
        maxCapacity = std::min(maxCapacity, tier.maxParticlesCap);
    }

    // ---- 1. Age & 2. Emitter Update ----
    std::uint64_t regularSpawnCount = 0ULL;
    std::uint64_t distanceSpawnCount = 0ULL;

    struct AgeSliceUVE final {
        float start = 0.0F;
        float end = 0.0F;
    };
    std::vector<AgeSliceUVE> activeSlices;

    const float safeDuration = std::max(1e-4F, m_duration);
    if (!m_stopped) {
        if (m_looping) {
            float remaining = dt;
            float cursor = state.emitterAge;
            while (remaining > 1e-7F) {
                const float roomInLoop = safeDuration - cursor;
                const float advance = std::min(remaining, roomInLoop);
                if (advance > 0.0F) {
                    activeSlices.push_back(AgeSliceUVE{cursor, cursor + advance});
                }
                remaining -= advance;
                cursor += advance;
                if (cursor >= safeDuration - 1e-7F) {
                    cursor = 0.0F;
                }
            }
            state.emitterAge = cursor;
        } else {
            const float prevAge = state.emitterAge;
            const float nextAge = prevAge + dt;
            if (prevAge < safeDuration) {
                const float clampedEnd = std::min(nextAge, safeDuration);
                if (clampedEnd > prevAge) {
                    activeSlices.push_back(AgeSliceUVE{prevAge, clampedEnd});
                }
            }
            state.emitterAge = nextAge;
        }

        if (!activeSlices.empty()) {
            const float ownerStepDist = Math::LengthUVE(owner.position - prevPos);
            for (std::size_t opIdx = 0U; opIdx < program.emitterUpdateOps.size(); ++opIdx) {
                const VfxEmitterUpdateOpUVE& op = program.emitterUpdateOps[opIdx];
                switch (op.type) {
                    case Asset::ParticleModuleTypeUVE::SpawnRate: {
                        for (const AgeSliceUVE& slice : activeSlices) {
                            const float midNormAge =
                                std::clamp((0.5F * (slice.start + slice.end)) / safeDuration, 0.0F, 1.0F);
                            const float rate = std::max(
                                0.0F, EvaluateCompiledFloatInputUVE(op.rate, midNormAge, nullptr,
                                                                    program.curveTables, m_parameters));
                            const float sliceDt = slice.end - slice.start;
                            state.spawnCarry[opIdx] += static_cast<double>(rate) * static_cast<double>(sliceDt) *
                                                       static_cast<double>(effectiveSpawnScale);
                            const std::uint32_t whole = static_cast<std::uint32_t>(std::floor(state.spawnCarry[opIdx]));
                            state.spawnCarry[opIdx] -= static_cast<double>(whole);
                            regularSpawnCount += whole;
                        }
                        break;
                    }
                    case Asset::ParticleModuleTypeUVE::SpawnBurst: {
                        for (const AgeSliceUVE& slice : activeSlices) {
                            const float startNormAge = std::clamp(slice.start / safeDuration, 0.0F, 1.0F);
                            regularSpawnCount += EvaluateBurstCrossingsUVE(
                                op, program, m_parameters, slice.start, slice.end, startNormAge, effectiveSpawnScale);
                        }
                        break;
                    }
                    case Asset::ParticleModuleTypeUVE::SpawnPerDistance: {
                        const float normAge = std::clamp(state.emitterAge / safeDuration, 0.0F, 1.0F);
                        const float perUnit = std::max(
                            0.0F, EvaluateCompiledFloatInputUVE(op.perUnit, normAge, nullptr,
                                                                program.curveTables, m_parameters));
                        state.spawnCarry[opIdx] += static_cast<double>(perUnit) *
                                                   static_cast<double>(ownerStepDist) *
                                                   static_cast<double>(effectiveSpawnScale);
                        const std::uint32_t whole = static_cast<std::uint32_t>(std::floor(state.spawnCarry[opIdx]));
                        state.spawnCarry[opIdx] -= static_cast<double>(whole);
                        distanceSpawnCount += whole;
                        break;
                    }
                    default:
                        break;
                }
            }
        }
    } else {
        state.emitterAge += dt;
    }

    // ---- 3. Pre-spawn Kill (expired / zero-lifetime safety pass) ----
    RunKillPassUVE(program, state, m_parameters);

    // ---- 4. Particle Spawn (clamped to emitter capacity) ----
    const std::size_t headroom =
        state.liveCount < maxCapacity ? static_cast<std::size_t>(maxCapacity) - state.liveCount : 0U;
    const std::uint32_t actualDistanceSpawn = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(distanceSpawnCount, static_cast<std::uint64_t>(headroom)));
    const std::size_t remainingHeadroom = headroom - actualDistanceSpawn;
    const std::uint32_t actualRegularSpawn = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(regularSpawnCount, static_cast<std::uint64_t>(remainingHeadroom)));

    if (actualDistanceSpawn > 0U) {
        for (std::uint32_t i = 0U; i < actualDistanceSpawn; ++i) {
            const float t = (static_cast<float>(i) + 0.5F) / static_cast<float>(actualDistanceSpawn);
            const Math::Vector3UVE origin =
                program.space == Asset::ParticleSimSpaceUVE::World ? LerpVec3UVE(prevPos, owner.position, t)
                                                                   : Math::Vector3UVE{};
            PushSpawnedParticleUVE(emitterIndex, program, state, m_instanceSeed, origin, owner, m_parameters);
        }
    }

    if (actualRegularSpawn > 0U) {
        const Math::Vector3UVE origin =
            program.space == Asset::ParticleSimSpaceUVE::World ? owner.position : Math::Vector3UVE{};
        for (std::uint32_t i = 0U; i < actualRegularSpawn; ++i) {
            PushSpawnedParticleUVE(emitterIndex, program, state, m_instanceSeed, origin, owner, m_parameters);
        }
    }

    // ---- 5. Particle Update & Integration ----
    const std::size_t count = state.liveCount;
    if (count > 0U) {
        const VfxAttributeMaskUVE mask = program.attributeMask;
        const bool hasPos = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Position);
        const bool hasVel = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Velocity);
        const bool hasColor = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Color);
        const bool hasSpawnColor = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnColor);
        const bool hasSize = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Size);
        const bool hasSpawnSize = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::SpawnSize);
        const bool hasRot = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Rotation);
        const bool hasSpin = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Spin);
        const bool hasFrame = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::Frame);
        const bool hasPrev = HasVfxAttributeUVE(mask, VfxAttributeBitUVE::PrevPosition);

        state.normalizedAge.resize(count);
        for (std::size_t i = 0U; i < count; ++i) {
            state.normalizedAge[i] =
                state.lifetime[i] > 1e-6F ? std::clamp(state.age[i] / state.lifetime[i], 0.0F, 1.0F) : 1.0F;
        }

        if (hasPos && hasPrev) {
            std::copy(state.posX.begin(), state.posX.end(), state.prevX.begin());
            std::copy(state.posY.begin(), state.posY.end(), state.prevY.begin());
            std::copy(state.posZ.begin(), state.posZ.end(), state.prevZ.begin());
        }

        if (hasVel) {
            state.accX.assign(count, 0.0F);
            state.accY.assign(count, 0.0F);
            state.accZ.assign(count, 0.0F);
        }

        const std::uint32_t stepIdx = state.stepCounter++;
        bool wroteSizeThisStep = false;

        for (const VfxParticleUpdateOpUVE& op : program.particleUpdateOps) {
            switch (op.type) {
                case Asset::ParticleModuleTypeUVE::Gravity: {
                    if (!hasVel) {
                        break;
                    }
                    if (op.vecA.kind == Asset::ParticleValueKindUVE::RandomRange) {
                        for (std::size_t i = 0U; i < count; ++i) {
                            Math::RandomUVE rng(HashVfxSeedUVE(state.seed[i], stepIdx ^ op.moduleId),
                                                static_cast<std::uint64_t>(emitterIndex));
                            const Math::Vector3UVE accel = EvaluateCompiledVectorInputUVE(op.vecA, &rng, m_parameters);
                            state.accX[i] += accel.x;
                            state.accY[i] += accel.y;
                            state.accZ[i] += accel.z;
                        }
                    } else {
                        const Math::Vector3UVE accel = EvaluateCompiledVectorInputUVE(op.vecA, nullptr, m_parameters);
                        for (std::size_t i = 0U; i < count; ++i) {
                            state.accX[i] += accel.x;
                            state.accY[i] += accel.y;
                            state.accZ[i] += accel.z;
                        }
                    }
                    break;
                }
                case Asset::ParticleModuleTypeUVE::Drag: {
                    if (!hasVel) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        Math::RandomUVE rng(HashVfxSeedUVE(state.seed[i], stepIdx ^ op.moduleId),
                                            static_cast<std::uint64_t>(emitterIndex));
                        const float c = EvaluateCompiledFloatInputUVE(
                            op.floatA, state.normalizedAge[i], &rng, program.curveTables, m_parameters);
                        state.accX[i] -= state.velX[i] * c;
                        state.accY[i] -= state.velY[i] * c;
                        state.accZ[i] -= state.velZ[i] * c;
                    }
                    break;
                }
                case Asset::ParticleModuleTypeUVE::AddVelocity: {
                    if (!hasVel) {
                        break;
                    }
                    if (op.vecA.kind == Asset::ParticleValueKindUVE::RandomRange) {
                        for (std::size_t i = 0U; i < count; ++i) {
                            Math::RandomUVE rng(HashVfxSeedUVE(state.seed[i], stepIdx ^ op.moduleId),
                                                static_cast<std::uint64_t>(emitterIndex));
                            const Math::Vector3UVE push = EvaluateCompiledVectorInputUVE(op.vecA, &rng, m_parameters);
                            state.velX[i] += push.x * dt;
                            state.velY[i] += push.y * dt;
                            state.velZ[i] += push.z * dt;
                        }
                    } else {
                        const Math::Vector3UVE push = EvaluateCompiledVectorInputUVE(op.vecA, nullptr, m_parameters);
                        for (std::size_t i = 0U; i < count; ++i) {
                            state.velX[i] += push.x * dt;
                            state.velY[i] += push.y * dt;
                            state.velZ[i] += push.z * dt;
                        }
                    }
                    break;
                }
                case Asset::ParticleModuleTypeUVE::LimitSpeed: {
                    if (!hasVel) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        Math::RandomUVE rng(HashVfxSeedUVE(state.seed[i], stepIdx ^ op.moduleId),
                                            static_cast<std::uint64_t>(emitterIndex));
                        const float maxSpeed = std::max(
                            0.0F, EvaluateCompiledFloatInputUVE(op.floatA, state.normalizedAge[i], &rng,
                                                                program.curveTables, m_parameters));
                        const float damping = std::clamp(
                            EvaluateCompiledFloatInputUVE(op.floatB, state.normalizedAge[i], &rng,
                                                          program.curveTables, m_parameters),
                            0.0F, 1.0F);
                        const float vx = state.velX[i];
                        const float vy = state.velY[i];
                        const float vz = state.velZ[i];
                        const float speedSq = vx * vx + vy * vy + vz * vz;
                        if (speedSq > maxSpeed * maxSpeed && speedSq > 1e-12F) {
                            const float speed = std::sqrt(speedSq);
                            const float targetRatio = maxSpeed / speed;
                            const float scale = Math::LerpUVE(1.0F, targetRatio, damping);
                            state.velX[i] = vx * scale;
                            state.velY[i] = vy * scale;
                            state.velZ[i] = vz * scale;
                        }
                    }
                    break;
                }
                case Asset::ParticleModuleTypeUVE::ColorOverLife: {
                    if (!hasColor || !hasSpawnColor) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        const VfxLinearColorAlphaUVE g = EvaluateCompiledColorInputUVE(
                            op.colorA, state.normalizedAge[i], nullptr, program.gradientTables, m_parameters);
                        state.colR[i] = state.spawnColR[i] * g.color.r;
                        state.colG[i] = state.spawnColG[i] * g.color.g;
                        state.colB[i] = state.spawnColB[i] * g.color.b;
                        state.colA[i] = state.spawnColA[i] * g.alpha;
                    }
                    break;
                }
                case Asset::ParticleModuleTypeUVE::SizeOverLife: {
                    if (!hasSize || !hasSpawnSize) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        const float scale = EvaluateCompiledFloatInputUVE(
                            op.floatA, state.normalizedAge[i], nullptr, program.curveTables, m_parameters);
                        state.sizeX[i] = state.spawnSizeX[i] * scale;
                        state.sizeY[i] = state.spawnSizeY[i] * scale;
                    }
                    wroteSizeThisStep = true;
                    break;
                }
                case Asset::ParticleModuleTypeUVE::RotationRate: {
                    if (!hasSpin) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        Math::RandomUVE rng(HashVfxSeedUVE(state.seed[i], stepIdx ^ op.moduleId),
                                            static_cast<std::uint64_t>(emitterIndex));
                        state.spin[i] = EvaluateCompiledFloatInputUVE(
                            op.floatA, state.normalizedAge[i], &rng, program.curveTables, m_parameters);
                    }
                    break;
                }
                case Asset::ParticleModuleTypeUVE::SizeBySpeed: {
                    if (!hasSize || !hasSpawnSize || !hasVel) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        const float t = state.normalizedAge[i];
                        const float minS = EvaluateCompiledFloatInputUVE(op.floatA, t, nullptr,
                                                                         program.curveTables, m_parameters);
                        const float maxS = EvaluateCompiledFloatInputUVE(op.floatB, t, nullptr,
                                                                         program.curveTables, m_parameters);
                        const float minScale = EvaluateCompiledFloatInputUVE(op.floatC, t, nullptr,
                                                                             program.curveTables, m_parameters);
                        const float maxScale = EvaluateCompiledFloatInputUVE(op.floatD, t, nullptr,
                                                                             program.curveTables, m_parameters);
                        const float vx = state.velX[i];
                        const float vy = state.velY[i];
                        const float vz = state.velZ[i];
                        const float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
                        const float u = (maxS > minS + 1e-6F) ? Math::SmoothStepUVE(minS, maxS, speed)
                                                              : (speed >= maxS ? 1.0F : 0.0F);
                        const float factor = Math::LerpUVE(minScale, maxScale, u);
                        if (wroteSizeThisStep) {
                            state.sizeX[i] *= factor;
                            state.sizeY[i] *= factor;
                        } else {
                            state.sizeX[i] = state.spawnSizeX[i] * factor;
                            state.sizeY[i] = state.spawnSizeY[i] * factor;
                        }
                    }
                    wroteSizeThisStep = true;
                    break;
                }
                case Asset::ParticleModuleTypeUVE::SpriteFlipbook: {
                    if (!hasFrame) {
                        break;
                    }
                    const std::int32_t totalFrames = std::max(1, op.intA * op.intB);
                    for (std::size_t i = 0U; i < count; ++i) {
                        if (op.intC == 1) {
                            const float fps = std::max(
                                0.0F, EvaluateCompiledFloatInputUVE(op.floatA, state.normalizedAge[i], nullptr,
                                                                    program.curveTables, m_parameters));
                            const std::int32_t f = static_cast<std::int32_t>(std::floor(state.age[i] * fps));
                            state.frame[i] = static_cast<float>(f % totalFrames);
                        } else {
                            const std::int32_t f = static_cast<std::int32_t>(
                                std::floor(state.normalizedAge[i] * static_cast<float>(totalFrames)));
                            state.frame[i] = static_cast<float>(std::clamp(f, 0, totalFrames - 1));
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // Integrate velocities, positions, rotations and ages.
        if (hasVel) {
            for (std::size_t i = 0U; i < count; ++i) {
                state.velX[i] += state.accX[i] * dt;
                state.velY[i] += state.accY[i] * dt;
                state.velZ[i] += state.accZ[i] * dt;
            }
            if (hasPos) {
                for (std::size_t i = 0U; i < count; ++i) {
                    state.posX[i] += state.velX[i] * dt;
                    state.posY[i] += state.velY[i] * dt;
                    state.posZ[i] += state.velZ[i] * dt;
                }

                // ---- Post-integration Collision Pass (CollisionPlane & real-time Collider3D CollisionPhysics) ----
                const bool hasSize = HasVfxAttributeUVE(program.attributeMask, VfxAttributeBitUVE::Size);
                for (const VfxParticleUpdateOpUVE& op : program.particleUpdateOps) {
                    if (op.type != Asset::ParticleModuleTypeUVE::CollisionPlane &&
                        op.type != Asset::ParticleModuleTypeUVE::CollisionPhysics) {
                        continue;
                    }
                    if (op.type == Asset::ParticleModuleTypeUVE::CollisionPhysics && sceneColliders.empty()) {
                        continue;
                    }

                    const bool killOnHit = op.intA != 0;
                    const bool isLocalSpace = program.space == Asset::ParticleSimSpaceUVE::Local;
                    const Math::QuaternionUVE invOwnerRot = Math::ConjugateUVE(owner.rotation);

                    if (op.type == Asset::ParticleModuleTypeUVE::CollisionPlane) {
                        for (std::size_t i = 0U; i < count; ++i) {
                            const float normAge = state.normalizedAge[i];
                            const Math::Vector3UVE planePoint =
                                EvaluateCompiledVectorInputUVE(op.vecA, nullptr, m_parameters);
                            Math::Vector3UVE planeNormal =
                                EvaluateCompiledVectorInputUVE(op.vecB, nullptr, m_parameters);
                            const float nLenSq = Math::LengthSquaredUVE(planeNormal);
                            planeNormal = nLenSq > 1.0e-8F ? (planeNormal * (1.0F / std::sqrt(nLenSq)))
                                                           : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                            const float bounce = std::clamp(
                                EvaluateCompiledFloatInputUVE(op.floatA, normAge, nullptr, program.curveTables,
                                                              m_parameters),
                                0.0F, 1.0F);
                            const float friction = std::clamp(
                                EvaluateCompiledFloatInputUVE(op.floatB, normAge, nullptr, program.curveTables,
                                                              m_parameters),
                                0.0F, 1.0F);
                            const float lifeLoss = std::max(
                                0.0F, EvaluateCompiledFloatInputUVE(op.floatC, normAge, nullptr, program.curveTables,
                                                                    m_parameters));
                            const float radius = hasSize ? std::max(0.005F, state.sizeX[i] * 0.25F) : 0.01F;
                            Math::Vector3UVE pos{state.posX[i], state.posY[i], state.posZ[i]};
                            const float signedDist = Math::DotUVE(pos - planePoint, planeNormal) - radius;
                            if (signedDist < 0.0F) {
                                pos += planeNormal * (-signedDist);
                                state.posX[i] = pos.x;
                                state.posY[i] = pos.y;
                                state.posZ[i] = pos.z;
                                Math::Vector3UVE vel{state.velX[i], state.velY[i], state.velZ[i]};
                                const float vDotN = Math::DotUVE(vel, planeNormal);
                                if (vDotN < 0.0F) {
                                    const Math::Vector3UVE vNormal = planeNormal * vDotN;
                                    const Math::Vector3UVE vTangent = vel - vNormal;
                                    vel = vTangent * (1.0F - friction) - vNormal * bounce;
                                    state.velX[i] = vel.x;
                                    state.velY[i] = vel.y;
                                    state.velZ[i] = vel.z;
                                }
                                if (killOnHit) {
                                    state.age[i] = state.lifetime[i] + 1.0F;
                                } else if (lifeLoss > 0.0F) {
                                    state.age[i] += lifeLoss * state.lifetime[i];
                                }
                            }
                        }
                        continue;
                    }

                    // Real-time CollisionPhysics against active scene Collider3D volumes (Box, Sphere, Capsule).
                    const std::size_t collisionCount = std::min<std::size_t>(count, 16384U);
                    for (std::size_t i = 0U; i < collisionCount; ++i) {
                        const float normAge = state.normalizedAge[i];
                        const float bounce = std::clamp(
                            EvaluateCompiledFloatInputUVE(op.floatA, normAge, nullptr, program.curveTables,
                                                          m_parameters),
                            0.0F, 1.0F);
                        const float friction = std::clamp(
                            EvaluateCompiledFloatInputUVE(op.floatB, normAge, nullptr, program.curveTables,
                                                          m_parameters),
                            0.0F, 1.0F);
                        const float lifeLoss = std::max(
                            0.0F, EvaluateCompiledFloatInputUVE(op.floatC, normAge, nullptr, program.curveTables,
                                                                m_parameters));
                        const float radius = hasSize ? std::max(0.005F, state.sizeX[i] * 0.25F) : 0.01F;

                        Math::Vector3UVE simPos{state.posX[i], state.posY[i], state.posZ[i]};
                        Math::Vector3UVE simVel{state.velX[i], state.velY[i], state.velZ[i]};
                        Math::Vector3UVE worldPos =
                            isLocalSpace ? (owner.position + Math::RotateVectorUVE(owner.rotation, simPos)) : simPos;
                        Math::Vector3UVE worldVel =
                            isLocalSpace ? Math::RotateVectorUVE(owner.rotation, simVel) : simVel;

                        bool collided = false;
                        for (const VfxColliderVolumeUVE& col : sceneColliders) {
                            Math::Vector3UVE hitNormal{0.0F, 1.0F, 0.0F};
                            float penetration = 0.0F;
                            bool hit = false;

                            if (col.kind == VfxColliderShapeKindUVE::Box) {
                                const Math::Vector3UVE ext{
                                    std::max(0.001F, col.halfExtents.x) + radius,
                                    std::max(0.001F, col.halfExtents.y) + radius,
                                    std::max(0.001F, col.halfExtents.z) + radius,
                                };
                                const Math::Vector3UVE centerDelta = worldPos - col.center;
                                if (Math::LengthSquaredUVE(centerDelta) > Math::LengthSquaredUVE(ext)) {
                                    continue;
                                }
                                const Math::QuaternionUVE invColRot = Math::ConjugateUVE(col.rotation);
                                const Math::Vector3UVE localP = Math::RotateVectorUVE(invColRot, centerDelta);
                                if (std::abs(localP.x) < ext.x && std::abs(localP.y) < ext.y &&
                                    std::abs(localP.z) < ext.z) {
                                    const float dx = ext.x - std::abs(localP.x);
                                    const float dy = ext.y - std::abs(localP.y);
                                    const float dz = ext.z - std::abs(localP.z);
                                    Math::Vector3UVE localNormal{0.0F, 1.0F, 0.0F};
                                    if (dy <= dx && dy <= dz) {
                                        penetration = dy;
                                        localNormal = {0.0F, localP.y >= 0.0F ? 1.0F : -1.0F, 0.0F};
                                    } else if (dx <= dz) {
                                        penetration = dx;
                                        localNormal = {localP.x >= 0.0F ? 1.0F : -1.0F, 0.0F, 0.0F};
                                    } else {
                                        penetration = dz;
                                        localNormal = {0.0F, 0.0F, localP.z >= 0.0F ? 1.0F : -1.0F};
                                    }
                                    hitNormal = Math::RotateVectorUVE(col.rotation, localNormal);
                                    hit = true;
                                }
                            } else if (col.kind == VfxColliderShapeKindUVE::Sphere) {
                                const Math::Vector3UVE delta = worldPos - col.center;
                                const float minDist = std::max(0.001F, col.radius) + radius;
                                const float distSq = Math::LengthSquaredUVE(delta);
                                if (distSq < minDist * minDist) {
                                    const float dist = std::sqrt(distSq);
                                    hitNormal = dist > 1.0e-5F ? (delta * (1.0F / dist))
                                                               : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                                    penetration = minDist - dist;
                                    hit = true;
                                }
                            } else if (col.kind == VfxColliderShapeKindUVE::Capsule) {
                                const float halfSeg = std::max(0.0F, col.height * 0.5F - col.radius);
                                const float minDist = std::max(0.001F, col.radius) + radius;
                                const float boundRadius = halfSeg + minDist;
                                const Math::Vector3UVE centerDelta = worldPos - col.center;
                                if (Math::LengthSquaredUVE(centerDelta) > boundRadius * boundRadius) {
                                    continue;
                                }
                                const Math::Vector3UVE up =
                                    Math::RotateVectorUVE(col.rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
                                const float t = std::clamp(Math::DotUVE(centerDelta, up), -halfSeg, halfSeg);
                                const Math::Vector3UVE closest = col.center + up * t;
                                const Math::Vector3UVE delta = worldPos - closest;
                                const float distSq = Math::LengthSquaredUVE(delta);
                                if (distSq < minDist * minDist) {
                                    const float dist = std::sqrt(distSq);
                                    hitNormal = dist > 1.0e-5F ? (delta * (1.0F / dist))
                                                               : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                                    penetration = minDist - dist;
                                    hit = true;
                                }
                            }

                            if (hit) {
                                collided = true;
                                worldPos += hitNormal * penetration;
                                const float vDotN = Math::DotUVE(worldVel, hitNormal);
                                if (vDotN < 0.0F) {
                                    const Math::Vector3UVE vNormal = hitNormal * vDotN;
                                    const Math::Vector3UVE vTangent = worldVel - vNormal;
                                    worldVel = vTangent * (1.0F - friction) - vNormal * bounce;
                                }
                            }
                        }

                        if (collided) {
                            simPos = isLocalSpace ? Math::RotateVectorUVE(invOwnerRot, worldPos - owner.position)
                                                  : worldPos;
                            simVel = isLocalSpace ? Math::RotateVectorUVE(invOwnerRot, worldVel) : worldVel;
                            state.posX[i] = simPos.x;
                            state.posY[i] = simPos.y;
                            state.posZ[i] = simPos.z;
                            state.velX[i] = simVel.x;
                            state.velY[i] = simVel.y;
                            state.velZ[i] = simVel.z;
                            if (killOnHit) {
                                state.age[i] = state.lifetime[i] + 1.0F;
                            } else if (lifeLoss > 0.0F) {
                                state.age[i] += lifeLoss * state.lifetime[i];
                            }
                        }
                    }
                }
            }
        }
        if (hasRot && hasSpin) {
            for (std::size_t i = 0U; i < count; ++i) {
                state.rotation[i] += state.spin[i] * dt;
            }
        }
        for (std::size_t i = 0U; i < count; ++i) {
            state.age[i] += dt;
        }
    }

    // ---- Post-step Kill & Bounds ----
    RunKillPassUVE(program, state, m_parameters);
    if (!m_looping && state.emitterAge >= safeDuration && state.liveCount == 0U) {
        state.completed = true;
    }
    UpdateLiveBoundsUVE(program, state, owner);
}

} // namespace UVE::VFX
