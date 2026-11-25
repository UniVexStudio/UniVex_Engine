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

constexpr float kMaxFrameDt = 0.1F;
constexpr float kMaxSubStepDt = 1.0F / 30.0F;
constexpr float kWarmUpStepDt = 1.0F / 30.0F;

[[nodiscard]] VfxParameterValue ConvertParameter(const Asset::ParticleUserParameter& param) noexcept {
    VfxParameterValue out;
    out.type = param.type;
    out.vector = param.value;
    out.alpha = param.alpha;
    return out;
}

[[nodiscard]] std::uint32_t EffectiveSeed(const ParticleProgram& program,
                                             const std::uint32_t instanceSeed) noexcept {
    return program.seed != 0U ? program.seed : instanceSeed;
}

[[nodiscard]] Math::Vector3 LerpVec3(const Math::Vector3& a,
                                           const Math::Vector3& b,
                                           const float t) noexcept {
    return Math::Vector3{
        Math::Lerp(a.x, b.x, t),
        Math::Lerp(a.y, b.y, t),
        Math::Lerp(a.z, b.z, t),
    };
}

[[nodiscard]] Math::Vector3 SampleVelocityInCone(Math::Random& rng,
                                                     const Math::Vector3& axisInput,
                                                     const float angleDegrees,
                                                     const float speed) noexcept {
    Math::Vector3 w{0.0F, 1.0F, 0.0F};
    if (Math::LengthSquared(axisInput) > 1e-12F) {
        w = Math::Normalize(axisInput);
    }
    const Math::Vector3 ref = std::abs(w.y) < 0.99F ? Math::Vector3{0.0F, 1.0F, 0.0F}
                                                       : Math::Vector3{1.0F, 0.0F, 0.0F};
    const Math::Vector3 uBasis = Math::Normalize(Math::Cross(ref, w));
    const Math::Vector3 vBasis = Math::Cross(w, uBasis);

    const float clampedAngle = std::clamp(angleDegrees, 0.0F, 180.0F);
    const float cosMax = std::cos(Math::DegToRad(clampedAngle));
    const float u = rng.NextFloat();
    const float v = rng.NextFloat();
    const float cosTheta = Math::Lerp(1.0F, cosMax, u);
    const float sinTheta = std::sqrt(std::max(0.0F, 1.0F - cosTheta * cosTheta));
    const float phi = 2.0F * Math::kPi * v;
    const Math::Vector3 dir =
        w * cosTheta + (uBasis * std::cos(phi) + vBasis * std::sin(phi)) * sinTheta;
    return dir * speed;
}

void ResetEmitterStateArrays(const ParticleProgram& program,
                                VfxEmitterState& state,
                                const VfxOwner& initialOwner) {
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

    state.liveBounds = Math::Aabb{};
    state.hasLiveBounds = false;
}

void SwapRemoveParticleAt(VfxEmitterState& state,
                             const VfxAttributeMask mask,
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

    if (HasVfxAttribute(mask, VfxAttributeBit::Position)) {
        removeFrom(state.posX);
        removeFrom(state.posY);
        removeFrom(state.posZ);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Velocity)) {
        removeFrom(state.velX);
        removeFrom(state.velY);
        removeFrom(state.velZ);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Color)) {
        removeFrom(state.colR);
        removeFrom(state.colG);
        removeFrom(state.colB);
        removeFrom(state.colA);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::SpawnColor)) {
        removeFrom(state.spawnColR);
        removeFrom(state.spawnColG);
        removeFrom(state.spawnColB);
        removeFrom(state.spawnColA);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Size)) {
        removeFrom(state.sizeX);
        removeFrom(state.sizeY);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::SpawnSize)) {
        removeFrom(state.spawnSizeX);
        removeFrom(state.spawnSizeY);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Rotation)) {
        removeFrom(state.rotation);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Spin)) {
        removeFrom(state.spin);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Frame)) {
        removeFrom(state.frame);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Mass)) {
        removeFrom(state.mass);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::PrevPosition)) {
        removeFrom(state.prevX);
        removeFrom(state.prevY);
        removeFrom(state.prevZ);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::SpawnOrigin)) {
        removeFrom(state.spawnX);
        removeFrom(state.spawnY);
        removeFrom(state.spawnZ);
    }

    --state.liveCount;
}

void PushSpawnedParticle(const std::size_t emitterIndex,
                            const ParticleProgram& program,
                            VfxEmitterState& state,
                            const std::uint32_t instanceSeed,
                            const Math::Vector3& spawnOriginPos,
                            const VfxOwner& owner,
                            const std::vector<VfxParameterValue>& parameters) {
    const std::uint32_t particleId = state.nextId++;
    const std::uint32_t particleSeed = HashVfxSeed(EffectiveSeed(program, instanceSeed), particleId);
    Math::Random rng(particleSeed, static_cast<std::uint64_t>(emitterIndex));

    Math::Vector3 localPos{};
    Math::Vector3 localVel{};
    float inheritOwnerVelScale = 0.0F;
    float particleLifetime = 1.0F;
    VfxLinearColorAlpha color{{1.0F, 1.0F, 1.0F}, 1.0F};
    float sizeX = 1.0F;
    float sizeY = 1.0F;
    float rotationDeg = 0.0F;
    float spinDeg = 0.0F;
    float spriteFrame = 0.0F;
    float particleMass = 1.0F;

    for (const VfxParticleSpawnOp& op : program.particleSpawnOps) {
        switch (op.type) {
            case Asset::ParticleModuleType::ShapePoint: {
                const VfxShapeSample sample = SampleShapePoint(rng);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInput(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleType::ShapeSphere: {
                const float radius = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const VfxShapeSample sample = SampleShapeSphere(rng, radius, op.surface != 0, op.hemisphere != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInput(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleType::ShapeBox: {
                const Math::Vector3 size = EvaluateCompiledVectorInput(op.vecA, &rng, parameters);
                const VfxShapeSample sample = SampleShapeBox(rng, size, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInput(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleType::ShapeCone: {
                const float angle = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                                  program.curveTables, parameters);
                const float length = EvaluateCompiledFloatInput(op.floatB, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const float radius = EvaluateCompiledFloatInput(op.floatC, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const VfxShapeSample sample = SampleShapeCone(rng, angle, length, radius, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInput(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleType::ShapeCylinder: {
                const float radius = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const float height = EvaluateCompiledFloatInput(op.floatB, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const VfxShapeSample sample = SampleShapeCylinder(rng, radius, height, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInput(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleType::ShapeTorus: {
                const float majorRadius = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                                        program.curveTables, parameters);
                const float minorRadius = EvaluateCompiledFloatInput(op.floatB, 0.0F, &rng,
                                                                        program.curveTables, parameters);
                const VfxShapeSample sample = SampleShapeTorus(rng, majorRadius, minorRadius, op.surface != 0);
                localPos = sample.position;
                const float ns = EvaluateCompiledFloatInput(op.normalSpeed, 0.0F, &rng,
                                                               program.curveTables, parameters);
                if (ns != 0.0F) {
                    localVel += sample.normal * ns;
                }
                break;
            }
            case Asset::ParticleModuleType::InitialVelocity:
                localVel = EvaluateCompiledVectorInput(op.vecA, &rng, parameters);
                break;
            case Asset::ParticleModuleType::VelocityInCone: {
                const Math::Vector3 axis = EvaluateCompiledVectorInput(op.vecA, &rng, parameters);
                const float angle = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                                  program.curveTables, parameters);
                const float speed = EvaluateCompiledFloatInput(op.floatB, 0.0F, &rng,
                                                                  program.curveTables, parameters);
                localVel = SampleVelocityInCone(rng, axis, angle, speed);
                break;
            }
            case Asset::ParticleModuleType::InitialLifetime:
                particleLifetime = std::max(
                    0.0F, EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng, program.curveTables, parameters));
                break;
            case Asset::ParticleModuleType::InitialColor:
                color = EvaluateCompiledColorInput(op.colorA, 0.0F, &rng, program.gradientTables, parameters);
                break;
            case Asset::ParticleModuleType::InitialSize: {
                const float sFloat = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                                   program.curveTables, parameters);
                const Math::Vector3 sVec = EvaluateCompiledVectorInput(op.vecA, &rng, parameters);
                sizeX = sFloat * sVec.x;
                sizeY = sFloat * sVec.y;
                break;
            }
            case Asset::ParticleModuleType::InitialRotation:
                rotationDeg = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                            program.curveTables, parameters);
                break;
            case Asset::ParticleModuleType::InitialSpin:
                spinDeg = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                        program.curveTables, parameters);
                break;
            case Asset::ParticleModuleType::InitialSpriteFrame:
                spriteFrame = EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                            program.curveTables, parameters);
                break;
            case Asset::ParticleModuleType::InitialMass:
                particleMass = std::max(
                    1e-4F, EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng, program.curveTables, parameters));
                break;
            case Asset::ParticleModuleType::InheritOwnerVelocity:
                inheritOwnerVelScale += EvaluateCompiledFloatInput(op.floatA, 0.0F, &rng,
                                                                      program.curveTables, parameters);
                break;
            default:
                break;
        }
    }

    Math::Vector3 finalPos = localPos;
    Math::Vector3 finalVel = localVel;
    if (program.space == Asset::ParticleSimSpace::World) {
        finalPos = spawnOriginPos + Math::RotateVector(owner.rotation, localPos);
        finalVel = Math::RotateVector(owner.rotation, localVel) + owner.velocity * inheritOwnerVelScale;
    } else if (inheritOwnerVelScale != 0.0F) {
        finalVel += Math::RotateVector(Math::Conjugate(owner.rotation), owner.velocity) *
                    inheritOwnerVelScale;
    }

    const VfxAttributeMask mask = program.attributeMask;
    state.id.push_back(particleId);
    state.seed.push_back(particleSeed);
    state.age.push_back(0.0F);
    state.lifetime.push_back(particleLifetime);

    if (HasVfxAttribute(mask, VfxAttributeBit::Position)) {
        state.posX.push_back(finalPos.x);
        state.posY.push_back(finalPos.y);
        state.posZ.push_back(finalPos.z);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Velocity)) {
        state.velX.push_back(finalVel.x);
        state.velY.push_back(finalVel.y);
        state.velZ.push_back(finalVel.z);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Color)) {
        state.colR.push_back(color.color.r);
        state.colG.push_back(color.color.g);
        state.colB.push_back(color.color.b);
        state.colA.push_back(color.alpha);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::SpawnColor)) {
        state.spawnColR.push_back(color.color.r);
        state.spawnColG.push_back(color.color.g);
        state.spawnColB.push_back(color.color.b);
        state.spawnColA.push_back(color.alpha);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Size)) {
        state.sizeX.push_back(sizeX);
        state.sizeY.push_back(sizeY);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::SpawnSize)) {
        state.spawnSizeX.push_back(sizeX);
        state.spawnSizeY.push_back(sizeY);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Rotation)) {
        state.rotation.push_back(rotationDeg);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Spin)) {
        state.spin.push_back(spinDeg);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Frame)) {
        state.frame.push_back(spriteFrame);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::Mass)) {
        state.mass.push_back(particleMass);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::PrevPosition)) {
        state.prevX.push_back(finalPos.x);
        state.prevY.push_back(finalPos.y);
        state.prevZ.push_back(finalPos.z);
    }
    if (HasVfxAttribute(mask, VfxAttributeBit::SpawnOrigin)) {
        state.spawnX.push_back(finalPos.x);
        state.spawnY.push_back(finalPos.y);
        state.spawnZ.push_back(finalPos.z);
    }

    ++state.liveCount;
}

[[nodiscard]] std::uint64_t EvaluateBurstCrossings(const VfxEmitterUpdateOp& op,
                                                      const ParticleProgram& program,
                                                      const std::vector<VfxParameterValue>& parameters,
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
        0.0F, EvaluateCompiledFloatInput(op.time, sliceNormAge, nullptr, program.curveTables, parameters));
    const float interval = std::max(
        0.0F, EvaluateCompiledFloatInput(op.interval, sliceNormAge, nullptr, program.curveTables, parameters));

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

void RunKillPass(const ParticleProgram& program,
                    VfxEmitterState& state,
                    const std::vector<VfxParameterValue>& parameters) {
    if (state.liveCount == 0U) {
        return;
    }

    const bool hasPosition = HasVfxAttribute(program.attributeMask, VfxAttributeBit::Position);
    std::vector<const VfxParticleUpdateOp*> killBoundsOps;
    for (const VfxParticleUpdateOp& op : program.particleUpdateOps) {
        if (op.type == Asset::ParticleModuleType::KillByBounds && hasPosition) {
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
            for (const VfxParticleUpdateOp* op : killBoundsOps) {
                const Math::Vector3 minV = EvaluateCompiledVectorInput(op->vecA, nullptr, parameters);
                const Math::Vector3 maxV = EvaluateCompiledVectorInput(op->vecB, nullptr, parameters);
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
            SwapRemoveParticleAt(state, program.attributeMask, i);
        }
    }
}

void UpdateLiveBounds(const ParticleProgram& program,
                         VfxEmitterState& state,
                         const VfxOwner& owner) {
    if (state.liveCount == 0U) {
        state.liveBounds = Math::Aabb{};
        state.hasLiveBounds = false;
        return;
    }

    if (!HasVfxAttribute(program.attributeMask, VfxAttributeBit::Position)) {
        const Math::Vector3 origin =
            program.space == Asset::ParticleSimSpace::World ? owner.position : Math::Vector3{};
        state.liveBounds = Math::Aabb{origin, origin};
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
    state.liveBounds = Math::Aabb{
        Math::Vector3{minX, minY, minZ},
        Math::Vector3{maxX, maxY, maxZ},
    };
    state.hasLiveBounds = true;
}

} // namespace

bool VfxInstance::Initialize(const Asset::ParticleSystemAsset& system,
                                   const VfxOwner& initialOwner,
                                   const std::uint32_t instanceSeed,
                                   const VfxCompileContext& compileContext,
                                   std::vector<ParticleCompileMessage>* const outMessages) {
    if (!Asset::IsParticleSystemAssetValid(system)) {
        if (outMessages != nullptr) {
            outMessages->push_back(ParticleCompileMessage{
                ParticleCompileMessage::Severity::Error,
                0U,
                "Particle system asset failed structural validation.",
            });
        }
        return false;
    }

    std::vector<EmitterRuntime> compiledEmitters;
    compiledEmitters.resize(system.emitters.size());
    if (outMessages != nullptr) {
        outMessages->clear();
    }

    for (std::size_t idx = 0U; idx < system.emitters.size(); ++idx) {
        std::vector<ParticleCompileMessage> emitterMessages;
        const bool ok = CompileParticleEmitter(system, idx, compileContext, compiledEmitters[idx].program,
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
        m_defaultParameters.push_back(ConvertParameter(system.parameters[idx]));
        m_parameterIndices.emplace(system.parameters[idx].name, idx);
    }
    m_parameters = m_defaultParameters;
    m_initialized = true;

    Reset(initialOwner);
    return true;
}

void VfxInstance::Reset(const VfxOwner& initialOwner) {
    m_stopped = false;
    m_killed = false;
    m_lastOwnerPos = initialOwner.position;
    for (EmitterRuntime& emitter : m_emitters) {
        ResetEmitterStateArrays(emitter.program, emitter.state, initialOwner);
    }

    if (m_initialized && m_warmUpSeconds > 0.0F) {
        float remaining = m_warmUpSeconds;
        while (remaining > 1e-6F) {
            const float step = std::min(remaining, kWarmUpStepDt);
            StepSingleSubstep(step, initialOwner.position, initialOwner, 1.0F, {});
            remaining -= step;
        }
    }
}

void VfxInstance::Stop() noexcept {
    m_stopped = true;
}

void VfxInstance::Kill() noexcept {
    m_stopped = true;
    m_killed = true;
    for (EmitterRuntime& emitter : m_emitters) {
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
        emitter.state.liveBounds = Math::Aabb{};
        emitter.state.hasLiveBounds = false;
    }
}

void VfxInstance::Tick(const float dt,
                             const VfxOwner& owner,
                             const float externalSpawnScale,
                             const std::span<const VfxColliderVolume> sceneColliders) {
    if (!m_initialized || m_killed) {
        return;
    }
    const float clampedDt = std::clamp(dt, 0.0F, kMaxFrameDt);
    if (clampedDt <= 0.0F) {
        return;
    }

    std::uint32_t substeps = 1U;
    if (clampedDt > kMaxSubStepDt + 1e-6F) {
        substeps = static_cast<std::uint32_t>(std::ceil(clampedDt * 30.0F));
        while (clampedDt / static_cast<float>(substeps) > kMaxSubStepDt + 1e-6F) {
            ++substeps;
        }
    }

    const float subDt = clampedDt / static_cast<float>(substeps);
    const Math::Vector3 frameStartPos = m_lastOwnerPos;

    for (std::uint32_t step = 0U; step < substeps; ++step) {
        const float startFrac = static_cast<float>(step) / static_cast<float>(substeps);
        const float endFrac = static_cast<float>(step + 1U) / static_cast<float>(substeps);
        VfxOwner stepOwner = owner;
        stepOwner.position = LerpVec3(frameStartPos, owner.position, endFrac);
        const Math::Vector3 prevStepPos = LerpVec3(frameStartPos, owner.position, startFrac);
        StepSingleSubstep(subDt, prevStepPos, stepOwner, externalSpawnScale, sceneColliders);
    }
    m_lastOwnerPos = owner.position;
}

bool VfxInstance::SetParameterFloat(const std::string_view name, const float value) noexcept {
    if (!std::isfinite(value)) {
        return false;
    }
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterType::Float) {
        return false;
    }
    m_parameters[it->second].vector.x = value;
    return true;
}

bool VfxInstance::SetParameterVector(const std::string_view name,
                                           const Math::Vector3& value) noexcept {
    if (!Math::IsFinite(value)) {
        return false;
    }
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterType::Vector) {
        return false;
    }
    m_parameters[it->second].vector = value;
    return true;
}

bool VfxInstance::SetParameterColor(const std::string_view name,
                                          const Math::Color& color,
                                          const float alpha) noexcept {
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
        !std::isfinite(alpha)) {
        return false;
    }
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterType::Color) {
        return false;
    }
    m_parameters[it->second].vector = Math::Vector3{color.r, color.g, color.b};
    m_parameters[it->second].alpha = alpha;
    return true;
}

bool VfxInstance::SetParameterBool(const std::string_view name, const bool value) noexcept {
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterType::Bool) {
        return false;
    }
    m_parameters[it->second].vector.x = value ? 1.0F : 0.0F;
    return true;
}

bool VfxInstance::SetParameterInt(const std::string_view name, const std::int32_t value) noexcept {
    const auto it = m_parameterIndices.find(std::string(name));
    if (it == m_parameterIndices.end() ||
        m_parameters[it->second].type != Asset::ParticleUserParameterType::Int) {
        return false;
    }
    m_parameters[it->second].vector.x = static_cast<float>(value);
    return true;
}

void VfxInstance::ClearParameterOverrides() noexcept {
    m_parameters = m_defaultParameters;
}

bool VfxInstance::IsAlive() const noexcept {
    if (!m_initialized || m_killed) {
        return false;
    }
    for (const EmitterRuntime& emitter : m_emitters) {
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

std::size_t VfxInstance::GetLiveCount() const noexcept {
    std::size_t total = 0U;
    for (const EmitterRuntime& emitter : m_emitters) {
        total += emitter.state.liveCount;
    }
    return total;
}

const ParticleProgram& VfxInstance::GetProgram(const std::size_t emitterIndex) const {
    if (emitterIndex >= m_emitters.size()) {
        throw std::out_of_range("VfxInstance emitter index out of range");
    }
    return m_emitters[emitterIndex].program;
}

const VfxEmitterState& VfxInstance::GetEmitterState(const std::size_t emitterIndex) const {
    if (emitterIndex >= m_emitters.size()) {
        throw std::out_of_range("VfxInstance emitter index out of range");
    }
    return m_emitters[emitterIndex].state;
}

Math::Aabb VfxInstance::GetLiveBounds() const noexcept {
    bool initialized = false;
    Math::Aabb combined{};
    for (const EmitterRuntime& emitter : m_emitters) {
        if (!emitter.state.hasLiveBounds) {
            continue;
        }
        combined = initialized ? combined.Union(emitter.state.liveBounds) : emitter.state.liveBounds;
        initialized = true;
    }
    return combined;
}

void VfxInstance::StepSingleSubstep(const float dt,
                                          const Math::Vector3& prevOwnerPos,
                                          const VfxOwner& owner,
                                          const float externalSpawnScale,
                                          const std::span<const VfxColliderVolume> sceneColliders) {
    for (std::size_t idx = 0U; idx < m_emitters.size(); ++idx) {
        StepEmitter(idx, m_emitters[idx], dt, prevOwnerPos, owner, externalSpawnScale, sceneColliders);
    }
}

void VfxInstance::StepEmitter(const std::size_t emitterIndex,
                                    EmitterRuntime& runtime,
                                    const float dt,
                                    const Math::Vector3& prevOwnerPos,
                                    const VfxOwner& owner,
                                    const float externalSpawnScale,
                                    const std::span<const VfxColliderVolume> sceneColliders) {
    const ParticleProgram& program = runtime.program;
    VfxEmitterState& state = runtime.state;
    if (!program.enabled) {
        return;
    }

    const Math::Vector3 prevPos = state.hasLastOwnerPos ? prevOwnerPos : owner.position;
    state.lastOwnerPos = owner.position;
    state.hasLastOwnerPos = true;

    const std::uint8_t quality = std::min<std::uint8_t>(owner.qualityLevel, 2U);
    const Asset::ParticleScalability& tier = m_scalability[quality];
    const float effectiveSpawnScale = std::max(0.0F, tier.spawnScale) * std::max(0.0F, externalSpawnScale);
    std::uint32_t maxCapacity = program.maxParticles;
    if (tier.maxParticlesCap > 0U) {
        maxCapacity = std::min(maxCapacity, tier.maxParticlesCap);
    }

    // ---- 1. Age & 2. Emitter Update ----
    std::uint64_t regularSpawnCount = 0ULL;
    std::uint64_t distanceSpawnCount = 0ULL;

    struct AgeSlice final {
        float start = 0.0F;
        float end = 0.0F;
    };
    std::vector<AgeSlice> activeSlices;

    const float safeDuration = std::max(1e-4F, m_duration);
    if (!m_stopped) {
        if (m_looping) {
            float remaining = dt;
            float cursor = state.emitterAge;
            while (remaining > 1e-7F) {
                const float roomInLoop = safeDuration - cursor;
                const float advance = std::min(remaining, roomInLoop);
                if (advance > 0.0F) {
                    activeSlices.push_back(AgeSlice{cursor, cursor + advance});
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
                    activeSlices.push_back(AgeSlice{prevAge, clampedEnd});
                }
            }
            state.emitterAge = nextAge;
        }

        if (!activeSlices.empty()) {
            const float ownerStepDist = Math::Length(owner.position - prevPos);
            for (std::size_t opIdx = 0U; opIdx < program.emitterUpdateOps.size(); ++opIdx) {
                const VfxEmitterUpdateOp& op = program.emitterUpdateOps[opIdx];
                switch (op.type) {
                    case Asset::ParticleModuleType::SpawnRate: {
                        for (const AgeSlice& slice : activeSlices) {
                            const float midNormAge =
                                std::clamp((0.5F * (slice.start + slice.end)) / safeDuration, 0.0F, 1.0F);
                            const float rate = std::max(
                                0.0F, EvaluateCompiledFloatInput(op.rate, midNormAge, nullptr,
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
                    case Asset::ParticleModuleType::SpawnBurst: {
                        for (const AgeSlice& slice : activeSlices) {
                            const float startNormAge = std::clamp(slice.start / safeDuration, 0.0F, 1.0F);
                            regularSpawnCount += EvaluateBurstCrossings(
                                op, program, m_parameters, slice.start, slice.end, startNormAge, effectiveSpawnScale);
                        }
                        break;
                    }
                    case Asset::ParticleModuleType::SpawnPerDistance: {
                        const float normAge = std::clamp(state.emitterAge / safeDuration, 0.0F, 1.0F);
                        const float perUnit = std::max(
                            0.0F, EvaluateCompiledFloatInput(op.perUnit, normAge, nullptr,
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
    RunKillPass(program, state, m_parameters);

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
            const Math::Vector3 origin =
                program.space == Asset::ParticleSimSpace::World ? LerpVec3(prevPos, owner.position, t)
                                                                   : Math::Vector3{};
            PushSpawnedParticle(emitterIndex, program, state, m_instanceSeed, origin, owner, m_parameters);
        }
    }

    if (actualRegularSpawn > 0U) {
        const Math::Vector3 origin =
            program.space == Asset::ParticleSimSpace::World ? owner.position : Math::Vector3{};
        for (std::uint32_t i = 0U; i < actualRegularSpawn; ++i) {
            PushSpawnedParticle(emitterIndex, program, state, m_instanceSeed, origin, owner, m_parameters);
        }
    }

    // ---- 5. Particle Update & Integration ----
    const std::size_t count = state.liveCount;
    if (count > 0U) {
        const VfxAttributeMask mask = program.attributeMask;
        const bool hasPos = HasVfxAttribute(mask, VfxAttributeBit::Position);
        const bool hasVel = HasVfxAttribute(mask, VfxAttributeBit::Velocity);
        const bool hasColor = HasVfxAttribute(mask, VfxAttributeBit::Color);
        const bool hasSpawnColor = HasVfxAttribute(mask, VfxAttributeBit::SpawnColor);
        const bool hasSize = HasVfxAttribute(mask, VfxAttributeBit::Size);
        const bool hasSpawnSize = HasVfxAttribute(mask, VfxAttributeBit::SpawnSize);
        const bool hasRot = HasVfxAttribute(mask, VfxAttributeBit::Rotation);
        const bool hasSpin = HasVfxAttribute(mask, VfxAttributeBit::Spin);
        const bool hasFrame = HasVfxAttribute(mask, VfxAttributeBit::Frame);
        const bool hasPrev = HasVfxAttribute(mask, VfxAttributeBit::PrevPosition);

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

        for (const VfxParticleUpdateOp& op : program.particleUpdateOps) {
            switch (op.type) {
                case Asset::ParticleModuleType::Gravity: {
                    if (!hasVel) {
                        break;
                    }
                    if (op.vecA.kind == Asset::ParticleValueKind::RandomRange) {
                        for (std::size_t i = 0U; i < count; ++i) {
                            Math::Random rng(HashVfxSeed(state.seed[i], stepIdx ^ op.moduleId),
                                                static_cast<std::uint64_t>(emitterIndex));
                            const Math::Vector3 accel = EvaluateCompiledVectorInput(op.vecA, &rng, m_parameters);
                            state.accX[i] += accel.x;
                            state.accY[i] += accel.y;
                            state.accZ[i] += accel.z;
                        }
                    } else {
                        const Math::Vector3 accel = EvaluateCompiledVectorInput(op.vecA, nullptr, m_parameters);
                        for (std::size_t i = 0U; i < count; ++i) {
                            state.accX[i] += accel.x;
                            state.accY[i] += accel.y;
                            state.accZ[i] += accel.z;
                        }
                    }
                    break;
                }
                case Asset::ParticleModuleType::Drag: {
                    if (!hasVel) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        Math::Random rng(HashVfxSeed(state.seed[i], stepIdx ^ op.moduleId),
                                            static_cast<std::uint64_t>(emitterIndex));
                        const float c = EvaluateCompiledFloatInput(
                            op.floatA, state.normalizedAge[i], &rng, program.curveTables, m_parameters);
                        state.accX[i] -= state.velX[i] * c;
                        state.accY[i] -= state.velY[i] * c;
                        state.accZ[i] -= state.velZ[i] * c;
                    }
                    break;
                }
                case Asset::ParticleModuleType::AddVelocity: {
                    if (!hasVel) {
                        break;
                    }
                    if (op.vecA.kind == Asset::ParticleValueKind::RandomRange) {
                        for (std::size_t i = 0U; i < count; ++i) {
                            Math::Random rng(HashVfxSeed(state.seed[i], stepIdx ^ op.moduleId),
                                                static_cast<std::uint64_t>(emitterIndex));
                            const Math::Vector3 push = EvaluateCompiledVectorInput(op.vecA, &rng, m_parameters);
                            state.velX[i] += push.x * dt;
                            state.velY[i] += push.y * dt;
                            state.velZ[i] += push.z * dt;
                        }
                    } else {
                        const Math::Vector3 push = EvaluateCompiledVectorInput(op.vecA, nullptr, m_parameters);
                        for (std::size_t i = 0U; i < count; ++i) {
                            state.velX[i] += push.x * dt;
                            state.velY[i] += push.y * dt;
                            state.velZ[i] += push.z * dt;
                        }
                    }
                    break;
                }
                case Asset::ParticleModuleType::LimitSpeed: {
                    if (!hasVel) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        Math::Random rng(HashVfxSeed(state.seed[i], stepIdx ^ op.moduleId),
                                            static_cast<std::uint64_t>(emitterIndex));
                        const float maxSpeed = std::max(
                            0.0F, EvaluateCompiledFloatInput(op.floatA, state.normalizedAge[i], &rng,
                                                                program.curveTables, m_parameters));
                        const float damping = std::clamp(
                            EvaluateCompiledFloatInput(op.floatB, state.normalizedAge[i], &rng,
                                                          program.curveTables, m_parameters),
                            0.0F, 1.0F);
                        const float vx = state.velX[i];
                        const float vy = state.velY[i];
                        const float vz = state.velZ[i];
                        const float speedSq = vx * vx + vy * vy + vz * vz;
                        if (speedSq > maxSpeed * maxSpeed && speedSq > 1e-12F) {
                            const float speed = std::sqrt(speedSq);
                            const float targetRatio = maxSpeed / speed;
                            const float scale = Math::Lerp(1.0F, targetRatio, damping);
                            state.velX[i] = vx * scale;
                            state.velY[i] = vy * scale;
                            state.velZ[i] = vz * scale;
                        }
                    }
                    break;
                }
                case Asset::ParticleModuleType::ColorOverLife: {
                    if (!hasColor || !hasSpawnColor) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        const VfxLinearColorAlpha g = EvaluateCompiledColorInput(
                            op.colorA, state.normalizedAge[i], nullptr, program.gradientTables, m_parameters);
                        state.colR[i] = state.spawnColR[i] * g.color.r;
                        state.colG[i] = state.spawnColG[i] * g.color.g;
                        state.colB[i] = state.spawnColB[i] * g.color.b;
                        state.colA[i] = state.spawnColA[i] * g.alpha;
                    }
                    break;
                }
                case Asset::ParticleModuleType::SizeOverLife: {
                    if (!hasSize || !hasSpawnSize) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        const float scale = EvaluateCompiledFloatInput(
                            op.floatA, state.normalizedAge[i], nullptr, program.curveTables, m_parameters);
                        state.sizeX[i] = state.spawnSizeX[i] * scale;
                        state.sizeY[i] = state.spawnSizeY[i] * scale;
                    }
                    wroteSizeThisStep = true;
                    break;
                }
                case Asset::ParticleModuleType::RotationRate: {
                    if (!hasSpin) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        Math::Random rng(HashVfxSeed(state.seed[i], stepIdx ^ op.moduleId),
                                            static_cast<std::uint64_t>(emitterIndex));
                        state.spin[i] = EvaluateCompiledFloatInput(
                            op.floatA, state.normalizedAge[i], &rng, program.curveTables, m_parameters);
                    }
                    break;
                }
                case Asset::ParticleModuleType::SizeBySpeed: {
                    if (!hasSize || !hasSpawnSize || !hasVel) {
                        break;
                    }
                    for (std::size_t i = 0U; i < count; ++i) {
                        const float t = state.normalizedAge[i];
                        const float minS = EvaluateCompiledFloatInput(op.floatA, t, nullptr,
                                                                         program.curveTables, m_parameters);
                        const float maxS = EvaluateCompiledFloatInput(op.floatB, t, nullptr,
                                                                         program.curveTables, m_parameters);
                        const float minScale = EvaluateCompiledFloatInput(op.floatC, t, nullptr,
                                                                             program.curveTables, m_parameters);
                        const float maxScale = EvaluateCompiledFloatInput(op.floatD, t, nullptr,
                                                                             program.curveTables, m_parameters);
                        const float vx = state.velX[i];
                        const float vy = state.velY[i];
                        const float vz = state.velZ[i];
                        const float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
                        const float u = (maxS > minS + 1e-6F) ? Math::SmoothStep(minS, maxS, speed)
                                                              : (speed >= maxS ? 1.0F : 0.0F);
                        const float factor = Math::Lerp(minScale, maxScale, u);
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
                case Asset::ParticleModuleType::SpriteFlipbook: {
                    if (!hasFrame) {
                        break;
                    }
                    const std::int32_t totalFrames = std::max(1, op.intA * op.intB);
                    for (std::size_t i = 0U; i < count; ++i) {
                        if (op.intC == 1) {
                            const float fps = std::max(
                                0.0F, EvaluateCompiledFloatInput(op.floatA, state.normalizedAge[i], nullptr,
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
                const bool hasSize = HasVfxAttribute(program.attributeMask, VfxAttributeBit::Size);
                for (const VfxParticleUpdateOp& op : program.particleUpdateOps) {
                    if (op.type != Asset::ParticleModuleType::CollisionPlane &&
                        op.type != Asset::ParticleModuleType::CollisionPhysics) {
                        continue;
                    }
                    if (op.type == Asset::ParticleModuleType::CollisionPhysics && sceneColliders.empty()) {
                        continue;
                    }

                    const bool killOnHit = op.intA != 0;
                    const bool isLocalSpace = program.space == Asset::ParticleSimSpace::Local;
                    const Math::Quaternion invOwnerRot = Math::Conjugate(owner.rotation);

                    if (op.type == Asset::ParticleModuleType::CollisionPlane) {
                        for (std::size_t i = 0U; i < count; ++i) {
                            const float normAge = state.normalizedAge[i];
                            const Math::Vector3 planePoint =
                                EvaluateCompiledVectorInput(op.vecA, nullptr, m_parameters);
                            Math::Vector3 planeNormal =
                                EvaluateCompiledVectorInput(op.vecB, nullptr, m_parameters);
                            const float nLenSq = Math::LengthSquared(planeNormal);
                            planeNormal = nLenSq > 1.0e-8F ? (planeNormal * (1.0F / std::sqrt(nLenSq)))
                                                           : Math::Vector3{0.0F, 1.0F, 0.0F};
                            const float bounce = std::clamp(
                                EvaluateCompiledFloatInput(op.floatA, normAge, nullptr, program.curveTables,
                                                              m_parameters),
                                0.0F, 1.0F);
                            const float friction = std::clamp(
                                EvaluateCompiledFloatInput(op.floatB, normAge, nullptr, program.curveTables,
                                                              m_parameters),
                                0.0F, 1.0F);
                            const float lifeLoss = std::max(
                                0.0F, EvaluateCompiledFloatInput(op.floatC, normAge, nullptr, program.curveTables,
                                                                    m_parameters));
                            const float radius = hasSize ? std::max(0.005F, state.sizeX[i] * 0.25F) : 0.01F;
                            Math::Vector3 pos{state.posX[i], state.posY[i], state.posZ[i]};
                            const float signedDist = Math::Dot(pos - planePoint, planeNormal) - radius;
                            if (signedDist < 0.0F) {
                                pos += planeNormal * (-signedDist);
                                state.posX[i] = pos.x;
                                state.posY[i] = pos.y;
                                state.posZ[i] = pos.z;
                                Math::Vector3 vel{state.velX[i], state.velY[i], state.velZ[i]};
                                const float vDotN = Math::Dot(vel, planeNormal);
                                if (vDotN < 0.0F) {
                                    const Math::Vector3 vNormal = planeNormal * vDotN;
                                    const Math::Vector3 vTangent = vel - vNormal;
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
                            EvaluateCompiledFloatInput(op.floatA, normAge, nullptr, program.curveTables,
                                                          m_parameters),
                            0.0F, 1.0F);
                        const float friction = std::clamp(
                            EvaluateCompiledFloatInput(op.floatB, normAge, nullptr, program.curveTables,
                                                          m_parameters),
                            0.0F, 1.0F);
                        const float lifeLoss = std::max(
                            0.0F, EvaluateCompiledFloatInput(op.floatC, normAge, nullptr, program.curveTables,
                                                                m_parameters));
                        const float radius = hasSize ? std::max(0.005F, state.sizeX[i] * 0.25F) : 0.01F;

                        Math::Vector3 simPos{state.posX[i], state.posY[i], state.posZ[i]};
                        Math::Vector3 simVel{state.velX[i], state.velY[i], state.velZ[i]};
                        Math::Vector3 worldPos =
                            isLocalSpace ? (owner.position + Math::RotateVector(owner.rotation, simPos)) : simPos;
                        Math::Vector3 worldVel =
                            isLocalSpace ? Math::RotateVector(owner.rotation, simVel) : simVel;

                        bool collided = false;
                        for (const VfxColliderVolume& col : sceneColliders) {
                            Math::Vector3 hitNormal{0.0F, 1.0F, 0.0F};
                            float penetration = 0.0F;
                            bool hit = false;

                            if (col.kind == VfxColliderShapeKind::Box) {
                                const Math::Vector3 ext{
                                    std::max(0.001F, col.halfExtents.x) + radius,
                                    std::max(0.001F, col.halfExtents.y) + radius,
                                    std::max(0.001F, col.halfExtents.z) + radius,
                                };
                                const Math::Vector3 centerDelta = worldPos - col.center;
                                if (Math::LengthSquared(centerDelta) > Math::LengthSquared(ext)) {
                                    continue;
                                }
                                const Math::Quaternion invColRot = Math::Conjugate(col.rotation);
                                const Math::Vector3 localP = Math::RotateVector(invColRot, centerDelta);
                                if (std::abs(localP.x) < ext.x && std::abs(localP.y) < ext.y &&
                                    std::abs(localP.z) < ext.z) {
                                    const float dx = ext.x - std::abs(localP.x);
                                    const float dy = ext.y - std::abs(localP.y);
                                    const float dz = ext.z - std::abs(localP.z);
                                    Math::Vector3 localNormal{0.0F, 1.0F, 0.0F};
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
                                    hitNormal = Math::RotateVector(col.rotation, localNormal);
                                    hit = true;
                                }
                            } else if (col.kind == VfxColliderShapeKind::Sphere) {
                                const Math::Vector3 delta = worldPos - col.center;
                                const float minDist = std::max(0.001F, col.radius) + radius;
                                const float distSq = Math::LengthSquared(delta);
                                if (distSq < minDist * minDist) {
                                    const float dist = std::sqrt(distSq);
                                    hitNormal = dist > 1.0e-5F ? (delta * (1.0F / dist))
                                                               : Math::Vector3{0.0F, 1.0F, 0.0F};
                                    penetration = minDist - dist;
                                    hit = true;
                                }
                            } else if (col.kind == VfxColliderShapeKind::Capsule) {
                                const float halfSeg = std::max(0.0F, col.height * 0.5F - col.radius);
                                const float minDist = std::max(0.001F, col.radius) + radius;
                                const float boundRadius = halfSeg + minDist;
                                const Math::Vector3 centerDelta = worldPos - col.center;
                                if (Math::LengthSquared(centerDelta) > boundRadius * boundRadius) {
                                    continue;
                                }
                                const Math::Vector3 up =
                                    Math::RotateVector(col.rotation, Math::Vector3{0.0F, 1.0F, 0.0F});
                                const float t = std::clamp(Math::Dot(centerDelta, up), -halfSeg, halfSeg);
                                const Math::Vector3 closest = col.center + up * t;
                                const Math::Vector3 delta = worldPos - closest;
                                const float distSq = Math::LengthSquared(delta);
                                if (distSq < minDist * minDist) {
                                    const float dist = std::sqrt(distSq);
                                    hitNormal = dist > 1.0e-5F ? (delta * (1.0F / dist))
                                                               : Math::Vector3{0.0F, 1.0F, 0.0F};
                                    penetration = minDist - dist;
                                    hit = true;
                                }
                            }

                            if (hit) {
                                collided = true;
                                worldPos += hitNormal * penetration;
                                const float vDotN = Math::Dot(worldVel, hitNormal);
                                if (vDotN < 0.0F) {
                                    const Math::Vector3 vNormal = hitNormal * vDotN;
                                    const Math::Vector3 vTangent = worldVel - vNormal;
                                    worldVel = vTangent * (1.0F - friction) - vNormal * bounce;
                                }
                            }
                        }

                        if (collided) {
                            simPos = isLocalSpace ? Math::RotateVector(invOwnerRot, worldPos - owner.position)
                                                  : worldPos;
                            simVel = isLocalSpace ? Math::RotateVector(invOwnerRot, worldVel) : worldVel;
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
    RunKillPass(program, state, m_parameters);
    if (!m_looping && state.emitterAge >= safeDuration && state.liveCount == 0U) {
        state.completed = true;
    }
    UpdateLiveBounds(program, state, owner);
}

} // namespace UVE::VFX
