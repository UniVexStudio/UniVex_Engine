// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_render_bridge.h"

#include <algorithm>
#include <cmath>

namespace UVE::Render {

bool ValidateParticleRenderSnapshotUVE(const ParticleRenderSnapshotUVE& snapshot,
                                       const std::size_t maximumItems) noexcept {
    if (snapshot.items.size() > maximumItems || snapshot.sourceParticleCount < snapshot.items.size() ||
        (!snapshot.truncated && snapshot.sourceParticleCount != snapshot.items.size())) {
        return false;
    }
    for (const ParticleRenderItemUVE& item : snapshot.items) {
        if (item.entity == Scene::kInvalidEntityUVE || !std::isfinite(item.position.x) ||
            !std::isfinite(item.position.y) || !std::isfinite(item.position.z) ||
            !std::isfinite(item.remainingLifetimeSeconds) || item.remainingLifetimeSeconds < 0.0F ||
            !std::isfinite(item.sortDepth) || item.sequence == 0U) {
            return false;
        }
    }
    return true;
}

ParticleRenderSnapshotUVE ParticleRenderBridgeUVE::ExtractUVE(
    const Scene::ParticleRuntimeUVE& runtime, const std::size_t maximumItems) {
    ParticleRenderSnapshotUVE snapshot;
    if (maximumItems == 0U) {
        snapshot.truncated = runtime.GetInstanceCountUVE() != 0U;
        return snapshot;
    }

    const Scene::ParticleRuntimeSnapshotUVE runtimeSnapshot = runtime.GetSnapshotUVE();
    for (const Scene::ParticleRuntimeInstanceSnapshotUVE& instance : runtimeSnapshot.instances) {
        if (!instance.enabled) {
            continue;
        }
        const std::optional<Scene::ParticleStateSnapshotUVE> particleSnapshot =
            runtime.GetParticleSnapshotUVE(instance.entity);
        if (!particleSnapshot.has_value()) {
            continue;
        }
        snapshot.sourceParticleCount += particleSnapshot->particles.size();
        for (const Scene::ParticleStateUVE& particle : particleSnapshot->particles) {
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

void ParticleRenderBridgeUVE::ExtractVfxUVE(
    const VFX::VfxWorldUVE& world,
    VfxDrawListUVE& outDrawList,
    const Math::Vector3UVE& cameraPosition,
    const Math::Vector3UVE& cameraForward,
    const std::size_t maxInstances) {
    outDrawList.ClearUVE();

    std::vector<VFX::VfxActiveInstanceRefUVE> activeRefs;
    world.CollectActiveInstancesUVE(activeRefs);
    if (activeRefs.empty()) {
        return;
    }

    Math::Vector3UVE safeForward{0.0F, 0.0F, -1.0F};
    const bool hasValidForward = Math::IsFiniteUVE(cameraForward) &&
                                 Math::LengthSquaredUVE(cameraForward) > 1.0e-8F;
    if (hasValidForward) {
        safeForward = Math::NormalizeUVE(cameraForward);
    }
    const Math::Vector3UVE safeCameraPos =
        Math::IsFiniteUVE(cameraPosition) ? cameraPosition : Math::Vector3UVE{};
    constexpr float kDegToRadUVE = 3.14159265358979323846F / 180.0F;

    for (const VFX::VfxActiveInstanceRefUVE& activeRef : activeRefs) {
        if (activeRef.instance == nullptr || !activeRef.instance->IsInitializedUVE()) {
            continue;
        }
        const VFX::VfxInstanceUVE& inst = *activeRef.instance;
        const VFX::VfxOwnerUVE owner = activeRef.owner != nullptr ? *activeRef.owner : VFX::VfxOwnerUVE{};
        Math::QuaternionUVE ownerRotation{};
        if (!Math::TryNormalizeUVE(owner.rotation, ownerRotation)) {
            ownerRotation = Math::QuaternionUVE{};
        }
        const Math::Vector3UVE ownerPosition =
            Math::IsFiniteUVE(owner.position) ? owner.position : Math::Vector3UVE{};

        const std::vector<VFX::VfxParameterValueUVE>& parameters = inst.GetParametersUVE();
        const float duration = std::max(inst.GetDurationUVE(), 1.0e-4F);
        const std::size_t emitterCount = inst.GetEmitterCountUVE();

        for (std::size_t emitterIdx = 0U; emitterIdx < emitterCount; ++emitterIdx) {
            const VFX::ParticleProgramUVE& program = inst.GetProgramUVE(emitterIdx);
            const VFX::VfxEmitterStateUVE& state = inst.GetEmitterStateUVE(emitterIdx);
            if (!program.enabled || state.liveCount == 0U || program.renderers.empty()) {
                continue;
            }

            const float emitterNormAge = std::clamp(state.emitterAge / duration, 0.0F, 1.0F);
            const VFX::VfxAttributeMaskUVE mask = program.attributeMask;
            const bool hasPos = VFX::HasVfxAttributeUVE(mask, VFX::VfxAttributeBitUVE::Position);
            const bool hasVel = VFX::HasVfxAttributeUVE(mask, VFX::VfxAttributeBitUVE::Velocity);
            const bool hasCol = VFX::HasVfxAttributeUVE(mask, VFX::VfxAttributeBitUVE::Color);
            const bool hasSize = VFX::HasVfxAttributeUVE(mask, VFX::VfxAttributeBitUVE::Size);
            const bool hasRot = VFX::HasVfxAttributeUVE(mask, VFX::VfxAttributeBitUVE::Rotation);
            const bool hasFrame = VFX::HasVfxAttributeUVE(mask, VFX::VfxAttributeBitUVE::Frame);
            const bool isLocalSpace = program.space == Asset::ParticleSimSpaceUVE::Local;

            for (const VFX::VfxRendererDescUVE& rendererDesc : program.renderers) {
                if (rendererDesc.type != Asset::ParticleModuleTypeUVE::SpriteRenderer) {
                    continue;
                }

                VfxSpriteBatchUVE batch;
                batch.facing = std::clamp(rendererDesc.facing, 0, 3);
                batch.blend = std::clamp(rendererDesc.blend, 0, 2);
                batch.rows = std::max(rendererDesc.rows, 1);
                batch.cols = std::max(rendererDesc.cols, 1);
                batch.frameBlend = rendererDesc.frameBlend != 0 ? 1 : 0;
                batch.stretchScale = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInputUVE(
                              rendererDesc.stretchScale, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.alphaCutoff = std::clamp(
                    VFX::EvaluateCompiledFloatInputUVE(
                        rendererDesc.alphaCutoff, emitterNormAge, nullptr, program.curveTables, parameters),
                    0.0F, 1.0F);
                batch.softDistance = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInputUVE(
                              rendererDesc.softDistance, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.cameraFadeNear = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInputUVE(
                              rendererDesc.cameraFadeNear, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.cameraFadeFar = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInputUVE(
                              rendererDesc.cameraFadeFar, emitterNormAge, nullptr, program.curveTables, parameters));
                batch.emissive = std::max(
                    0.0F, VFX::EvaluateCompiledFloatInputUVE(
                              rendererDesc.emissive, emitterNormAge, nullptr, program.curveTables, parameters));
                Math::Vector3UVE axis =
                    VFX::EvaluateCompiledVectorInputUVE(rendererDesc.axis, nullptr, parameters);
                if (!Math::IsFiniteUVE(axis) || Math::LengthSquaredUVE(axis) < 1.0e-8F) {
                    axis = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                } else {
                    axis = Math::NormalizeUVE(axis);
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
                    Math::Vector3UVE pos = hasPos
                        ? Math::Vector3UVE{state.posX[i], state.posY[i], state.posZ[i]}
                        : Math::Vector3UVE{};
                    Math::Vector3UVE vel = hasVel
                        ? Math::Vector3UVE{state.velX[i], state.velY[i], state.velZ[i]}
                        : Math::Vector3UVE{};
                    if (isLocalSpace) {
                        pos = ownerPosition + Math::RotateVectorUVE(ownerRotation, pos);
                        vel = Math::RotateVectorUVE(ownerRotation, vel);
                    }
                    if (!Math::IsFiniteUVE(pos)) {
                        continue;
                    }
                    if (!Math::IsFiniteUVE(vel)) {
                        vel = Math::Vector3UVE{};
                    }

                    const float colR = hasCol ? state.colR[i] : 1.0F;
                    const float colG = hasCol ? state.colG[i] : 1.0F;
                    const float colB = hasCol ? state.colB[i] : 1.0F;
                    const float colA = hasCol ? state.colA[i] : 1.0F;
                    const Math::Vector2UVE size = hasSize
                        ? Math::Vector2UVE{std::max(0.0F, state.sizeX[i]), std::max(0.0F, state.sizeY[i])}
                        : Math::Vector2UVE{1.0F, 1.0F};
                    const float rotRad = (hasRot && std::isfinite(state.rotation[i]))
                        ? (state.rotation[i] * kDegToRadUVE)
                        : 0.0F;
                    const float frame = (hasFrame && std::isfinite(state.frame[i])) ? state.frame[i] : 0.0F;

                    batch.instances.push_back(MakeVfxSpriteGpuInstanceUVE(
                        pos, rotRad, vel, frame, size, colR, colG, colB, colA));

                    const Math::Vector3UVE relToCam = pos - safeCameraPos;
                    const float depth = hasValidForward
                        ? Math::DotUVE(relToCam, safeForward)
                        : Math::LengthUVE(relToCam);
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

