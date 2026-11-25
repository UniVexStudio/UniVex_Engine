// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_render_bridge.h"

#include <algorithm>
#include <cmath>

namespace UVE::Render {

bool ValidateParticleRenderSnapshot(const ParticleRenderSnapshot& snapshot,
                                       const std::size_t maximumItems) noexcept {
    if (snapshot.items.size() > maximumItems || snapshot.sourceParticleCount < snapshot.items.size() ||
        (!snapshot.truncated && snapshot.sourceParticleCount != snapshot.items.size())) {
        return false;
    }
    for (const ParticleRenderItem& item : snapshot.items) {
        if (item.entity == Scene::kInvalidEntity || !std::isfinite(item.position.x) ||
            !std::isfinite(item.position.y) || !std::isfinite(item.position.z) ||
            !std::isfinite(item.remainingLifetimeSeconds) || item.remainingLifetimeSeconds < 0.0F ||
            !std::isfinite(item.sortDepth) || item.sequence == 0U) {
            return false;
        }
    }
    return true;
}

ParticleRenderSnapshot ParticleRenderBridge::Extract(
    const Scene::ParticleRuntime& runtime, const std::size_t maximumItems) {
    ParticleRenderSnapshot snapshot;
    if (maximumItems == 0U) {
        snapshot.truncated = runtime.GetInstanceCount() != 0U;
        return snapshot;
    }

    const Scene::ParticleRuntimeSnapshot runtimeSnapshot = runtime.GetSnapshot();
    for (const Scene::ParticleRuntimeInstanceSnapshot& instance : runtimeSnapshot.instances) {
        if (!instance.enabled) {
            continue;
        }
        const std::optional<Scene::ParticleStateSnapshot> particleSnapshot =
            runtime.GetParticleSnapshot(instance.entity);
        if (!particleSnapshot.has_value()) {
            continue;
        }
        snapshot.sourceParticleCount += particleSnapshot->particles.size();
        for (const Scene::ParticleState& particle : particleSnapshot->particles) {
            if (snapshot.items.size() >= maximumItems) {
                snapshot.truncated = true;
                continue;
            }
            snapshot.items.push_back({instance.entity, particle.position, particle.remainingLifetimeSeconds,
                                      particle.position.z, particle.sequence});
        }
    }
    if (snapshot.sourceParticleCount > snapshot.items.size()) {
        snapshot.truncated = true;
    }
    return snapshot;
}

void ParticleRenderBridge::ExtractVfx(
    const VFX::VfxWorld& world,
    VfxDrawList& outDrawList,
    const Math::Vector3& cameraPosition,
    const Math::Vector3& cameraForward,
    const std::size_t maxInstances) {
    outDrawList.Clear();

    std::vector<VFX::VfxActiveInstanceRef> activeRefs;
    world.CollectActiveInstances(activeRefs);
    if (activeRefs.empty()) {
        return;
    }

    Math::Vector3 safeForward{0.0F, 0.0F, -1.0F};
    const bool hasValidForward = Math::IsFinite(cameraForward) &&
                                 Math::LengthSquared(cameraForward) > 1.0e-8F;
    if (hasValidForward) {
        safeForward = Math::Normalize(cameraForward);
    }
    const Math::Vector3 safeCameraPos =
        Math::IsFinite(cameraPosition) ? cameraPosition : Math::Vector3{};
    constexpr float kDegToRad = 3.14159265358979323846F / 180.0F;

    for (const VFX::VfxActiveInstanceRef& activeRef : activeRefs) {
        if (activeRef.instance == nullptr || !activeRef.instance->IsInitialized()) {
            continue;
        }
        const VFX::VfxInstance& inst = *activeRef.instance;
        const VFX::VfxOwner owner = activeRef.owner != nullptr ? *activeRef.owner : VFX::VfxOwner{};
        Math::Quaternion ownerRotation{};
        if (!Math::TryNormalize(owner.rotation, ownerRotation)) {
            ownerRotation = Math::Quaternion{};
        }
        const Math::Vector3 ownerPosition =
            Math::IsFinite(owner.position) ? owner.position : Math::Vector3{};

        const std::vector<VFX::VfxParameterValue>& parameters = inst.GetParameters();
        const float duration = std::max(inst.GetDuration(), 1.0e-4F);
        const std::size_t emitterCount = inst.GetEmitterCount();

        for (std::size_t emitterIdx = 0U; emitterIdx < emitterCount; ++emitterIdx) {
            const VFX::ParticleProgram& program = inst.GetProgram(emitterIdx);
            const VFX::VfxEmitterState& state = inst.GetEmitterState(emitterIdx);
            if (!program.enabled || state.liveCount == 0U || program.renderers.empty()) {
                continue;
            }

            const float emitterNormAge = std::clamp(state.emitterAge / duration, 0.0F, 1.0F);
            const VFX::VfxAttributeMask mask = program.attributeMask;
            const bool hasPos = VFX::HasVfxAttribute(mask, VFX::VfxAttributeBit::Position);
            const bool hasVel = VFX::HasVfxAttribute(mask, VFX::VfxAttributeBit::Velocity);
            const bool hasCol = VFX::HasVfxAttribute(mask, VFX::VfxAttributeBit::Color);
            const bool hasSize = VFX::HasVfxAttribute(mask, VFX::VfxAttributeBit::Size);
            const bool hasRot = VFX::HasVfxAttribute(mask, VFX::VfxAttributeBit::Rotation);
            const bool hasFrame = VFX::HasVfxAttribute(mask, VFX::VfxAttributeBit::Frame);
            const bool isLocalSpace = program.space == Asset::ParticleSimSpace::Local;

            for (const VFX::VfxRendererDesc& rendererDesc : program.renderers) {
                if (rendererDesc.type != Asset::ParticleModuleType::SpriteRenderer) {
                    continue;
                }

                VfxSpriteBatch batch;
                batch.facing = std::clamp(rendererDesc.facing, 0, 3);
                batch.blend = std::clamp(rendererDesc.blend, 0, 2);
                batch.rows = std::max(rendererDesc.rows, 1);
                batch.cols = std::max(rendererDesc.cols, 1);
                batch.frameBlend = rendererDesc.frameBlend != 0 ? 1 : 0;
                batch.stretchScale = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInput(
                              rendererDesc.stretchScale, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.alphaCutoff = std::clamp(
                    VFX::EvaluateCompiledFloatInput(
                        rendererDesc.alphaCutoff, emitterNormAge, nullptr, program.curveTables, parameters),
                    0.0F, 1.0F);
                batch.softDistance = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInput(
                              rendererDesc.softDistance, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.cameraFadeNear = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInput(
                              rendererDesc.cameraFadeNear, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.cameraFadeFar = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInput(
                              rendererDesc.cameraFadeFar, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.emissive = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInput(
                              rendererDesc.emissive, emitterNormAge, nullptr, program.curveTables, parameters));
                Math::Vector3 axis =
                    VFX::EvaluateCompiledVectorInput(rendererDesc.axis, nullptr, parameters);
                if (!Math::IsFinite(axis) || Math::LengthSquared(axis) < 1.0e-8F) {
                    axis = Math::Vector3{0.0F, 1.0F, 0.0F};
                } else {
                    axis = Math::Normalize(axis);
                }
                batch.axis = axis;
                batch.texturePath = rendererDesc.texturePath;
                batch.materialPath = rendererDesc.materialPath;
                batch.attachEntityKey = activeRef.attachEntityKey;

                const std::size_t remainingBudget =
                    outDrawList.stats.instancesExtracted < maxInstances
                        ? (maxInstances - outDrawList.stats.instancesExtracted)
                        : 0U;
                const std::size_t countToExtract = std::min(state.liveCount, remainingBudget);
                if (state.liveCount > countToExtract) {
                    outDrawList.stats.instancesTruncated += (state.liveCount - countToExtract);
                }
                if (countToExtract == 0U) {
                    continue;
                }

                batch.instances.reserve(countToExtract);
                batch.sortDepths.reserve(countToExtract);

                for (std::size_t i = 0U; i < countToExtract; ++i) {
                    Math::Vector3 pos = hasPos
                        ? Math::Vector3{state.posX[i], state.posY[i], state.posZ[i]}
                        : Math::Vector3{};
                    Math::Vector3 vel = hasVel
                        ? Math::Vector3{state.velX[i], state.velY[i], state.velZ[i]}
                        : Math::Vector3{};
                    if (isLocalSpace) {
                        pos = ownerPosition + Math::RotateVector(ownerRotation, pos);
                        vel = Math::RotateVector(ownerRotation, vel);
                    }
                    if (!Math::IsFinite(pos)) {
                        continue;
                    }
                    if (!Math::IsFinite(vel)) {
                        vel = Math::Vector3{};
                    }

                    const float colR = hasCol ? state.colR[i] : 1.0F;
                    const float colG = hasCol ? state.colG[i] : 1.0F;
                    const float colB = hasCol ? state.colB[i] : 1.0F;
                    const float colA = hasCol ? state.colA[i] : 1.0F;
                    const Math::Vector2 size = hasSize
                        ? Math::Vector2{std::max(0.0F, state.sizeX[i]), std::max(0.0F, state.sizeY[i])}
                        : Math::Vector2{1.0F, 1.0F};
                    const float rotRad = (hasRot && std::isfinite(state.rotation[i]))
                        ? (state.rotation[i] * kDegToRad)
                        : 0.0F;
                    const float frame = (hasFrame && std::isfinite(state.frame[i])) ? state.frame[i] : 0.0F;

                    batch.instances.push_back(MakeVfxSpriteGpuInstance(
                        pos, rotRad, vel, frame, size, colR, colG, colB, colA));

                    const Math::Vector3 relToCam = pos - safeCameraPos;
                    const float depth = hasValidForward
                        ? Math::Dot(relToCam, safeForward)
                        : Math::Length(relToCam);
                    batch.sortDepths.push_back(std::isfinite(depth) ? depth : 0.0F);
                }

                if (!batch.instances.empty()) {
                    outDrawList.stats.instancesExtracted += batch.instances.size();
                    ++outDrawList.stats.batchesExtracted;
                    if (batch.blend == 2) {
                        ++outDrawList.stats.opaqueBatchesExtracted;
                    } else {
                        ++outDrawList.stats.translucentBatchesExtracted;
                    }
                    outDrawList.batches.push_back(std::move(batch));
                }
            }
        }
    }
}

} // namespace UVE::Render

