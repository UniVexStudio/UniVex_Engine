// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_world.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

#include "logging_macros.h"
#include "matrix4x4.h"

namespace UVE::VFX {
namespace {

constexpr float kMaxInstanceStepDtUVE = 0.1F;

[[nodiscard]] bool IsSafeAssetQueryPathUVE(const std::string_view assetPath) noexcept {
    if (assetPath.empty() || assetPath.find('\0') != std::string_view::npos) {
        return false;
    }
    return true;
}

[[nodiscard]] std::string NormalizeCacheKeyUVE(const std::filesystem::path& resolvedPath) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolvedPath, ec);
    if (!ec && !canonical.empty()) {
        return canonical.generic_string();
    }
    return resolvedPath.lexically_normal().generic_string();
}

void AdvanceInstanceByTotalDtUVE(VfxInstanceUVE& instance,
                                 float totalDt,
                                 const VfxOwnerUVE& owner,
                                 const float externalSpawnScale,
                                 const std::span<const VfxColliderVolumeUVE> sceneColliders) {
    while (totalDt > 1e-6F) {
        const float stepDt = std::min(totalDt, kMaxInstanceStepDtUVE);
        instance.TickUVE(stepDt, owner, externalSpawnScale, sceneColliders);
        totalDt -= stepDt;
    }
}

[[nodiscard]] bool ValidateAndPrecompileAssetUVE(const Asset::ParticleSystemAssetUVE& asset,
                                                 const VfxCompileContextUVE& compileContext) {
    if (!Asset::IsParticleSystemAssetValidUVE(asset)) {
        return false;
    }
    std::vector<ParticleCompileMessageUVE> messages;
    for (std::size_t idx = 0U; idx < asset.emitters.size(); ++idx) {
        ParticleProgramUVE program;
        if (!CompileParticleEmitterUVE(asset, idx, compileContext, program, messages)) {
            return false;
        }
    }
    return true;
}

} // namespace

VfxWorldUVE::VfxWorldUVE() {
#if defined(__ANDROID__)
    m_qualityLevel = 0U;
#else
    m_qualityLevel = 2U;
#endif
    m_stats.globalBudget = m_globalBudget;
}

void VfxWorldUVE::SetProjectRootUVE(const std::filesystem::path& projectRoot) {
    m_projectRoot = projectRoot;
}

void VfxWorldUVE::SetGlobalParticleBudgetUVE(const std::size_t maxLiveParticles) noexcept {
    m_globalBudget = maxLiveParticles;
    m_stats.globalBudget = maxLiveParticles;
}

void VfxWorldUVE::SetQualityLevelUVE(const std::uint8_t level) noexcept {
    m_qualityLevel = std::min<std::uint8_t>(level, 2U);
}

void VfxWorldUVE::ClearCacheUVE() {
    m_assetCache.clear();
    UpdateSummaryStatsUVE();
}

std::filesystem::path VfxWorldUVE::ResolveAssetPathUVE(const std::string_view assetPath) const {
    if (!IsSafeAssetQueryPathUVE(assetPath)) {
        return {};
    }
    const std::filesystem::path rel(assetPath);
    const auto checkRegular = [](const std::filesystem::path& candidate) -> bool {
        std::error_code ec;
        return std::filesystem::is_regular_file(candidate, ec) && !ec;
    };

    if (rel.is_absolute()) {
        return checkRegular(rel) ? rel : std::filesystem::path{};
    }

    if (!m_projectRoot.empty()) {
        for (const std::filesystem::path& candidate : {
                 m_projectRoot / rel,
                 m_projectRoot / "content" / rel,
                 m_projectRoot / "assets" / rel,
             }) {
            if (checkRegular(candidate)) {
                return candidate;
            }
        }
    }

    for (const std::filesystem::path& candidate : {
             rel,
             std::filesystem::path("content") / rel,
             std::filesystem::path("assets") / rel,
         }) {
        if (checkRegular(candidate)) {
            return candidate;
        }
    }

    return {};
}

VfxHandleUVE VfxWorldUVE::SpawnUVE(const std::string_view assetPath,
                                   const VfxOwnerUVE& owner,
                                   const std::uint64_t attachEntityKey,
                                   const std::uint32_t instanceSeed) {
    const std::filesystem::path resolved = ResolveAssetPathUVE(assetPath);
    if (resolved.empty()) {
        UVE_WARNING("VfxWorldUVE: could not resolve particle system asset path \"{}\"", assetPath);
        return kInvalidVfxHandleUVE;
    }

    const std::string cacheKey = NormalizeCacheKeyUVE(resolved);
    auto cacheIt = m_assetCache.find(cacheKey);
    if (cacheIt == m_assetCache.end()) {
        Asset::ParticleSystemAssetUVE loadedAsset;
        if (!Asset::LoadParticleSystemAssetUVE(resolved, loadedAsset)) {
            UVE_WARNING("VfxWorldUVE: failed to load particle system asset \"{}\"", resolved.string());
            return kInvalidVfxHandleUVE;
        }
        const VfxCompileContextUVE compileContext{m_projectRoot};
        if (!ValidateAndPrecompileAssetUVE(loadedAsset, compileContext)) {
            UVE_WARNING("VfxWorldUVE: failed to compile particle system asset \"{}\"", resolved.string());
            return kInvalidVfxHandleUVE;
        }

        std::error_code ec;
        CachedAssetEntryUVE entry;
        entry.resolvedDiskPath = resolved;
        entry.isDiskAsset = true;
        entry.lastWriteTime = std::filesystem::last_write_time(resolved, ec);
        entry.fileSize = ec ? 0U : std::filesystem::file_size(resolved, ec);
        entry.asset = std::move(loadedAsset);
        cacheIt = m_assetCache.emplace(cacheKey, std::move(entry)).first;
    }

    return AllocateAndInitializeSlotUVE(cacheKey, cacheIt->second.asset, owner, attachEntityKey, instanceSeed);
}

VfxHandleUVE VfxWorldUVE::SpawnFromAssetUVE(const std::string_view cacheKey,
                                            const Asset::ParticleSystemAssetUVE& system,
                                            const VfxOwnerUVE& owner,
                                            const std::uint64_t attachEntityKey,
                                            const std::uint32_t instanceSeed) {
    std::string normalizedKey;
    if (!cacheKey.empty()) {
        normalizedKey.assign(cacheKey);
        auto it = m_assetCache.find(normalizedKey);
        if (it == m_assetCache.end()) {
            const VfxCompileContextUVE compileContext{m_projectRoot};
            if (!ValidateAndPrecompileAssetUVE(system, compileContext)) {
                return kInvalidVfxHandleUVE;
            }
            CachedAssetEntryUVE entry;
            entry.isDiskAsset = false;
            entry.asset = system;
            m_assetCache.emplace(normalizedKey, std::move(entry));
        }
    }
    return AllocateAndInitializeSlotUVE(normalizedKey, system, owner, attachEntityKey, instanceSeed);
}

bool VfxWorldUVE::StopUVE(const VfxHandleUVE handle) noexcept {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->instance.StopUVE();
    if (!slot->instance.IsAliveUVE()) {
        RetireSlotUVE(handle.index, *slot);
        UpdateSummaryStatsUVE();
    }
    return true;
}

bool VfxWorldUVE::KillUVE(const VfxHandleUVE handle) noexcept {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->instance.KillUVE();
    RetireSlotUVE(handle.index, *slot);
    UpdateSummaryStatsUVE();
    return true;
}

void VfxWorldUVE::ClearAllInstancesUVE() noexcept {
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        if (m_slots[idx].active) {
            m_slots[idx].instance.KillUVE();
            RetireSlotUVE(idx, m_slots[idx]);
        }
    }
    UpdateSummaryStatsUVE();
}

bool VfxWorldUVE::IsAliveUVE(const VfxHandleUVE handle) const noexcept {
    const InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    return slot != nullptr && slot->instance.IsAliveUVE();
}

bool VfxWorldUVE::SetOwnerUVE(const VfxHandleUVE handle, const VfxOwnerUVE& owner) noexcept {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->owner = owner;
    slot->owner.qualityLevel = m_qualityLevel;
    return true;
}

bool VfxWorldUVE::SetParameterFloatUVE(const VfxHandleUVE handle,
                                       const std::string_view name,
                                       const float value) {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr || !slot->instance.SetParameterFloatUVE(name, value)) {
        return false;
    }
    ParameterOverrideUVE overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterTypeUVE::Float;
    overrideEntry.vector.x = value;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorldUVE::SetParameterVectorUVE(const VfxHandleUVE handle,
                                        const std::string_view name,
                                        const Math::Vector3UVE& value) {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr || !slot->instance.SetParameterVectorUVE(name, value)) {
        return false;
    }
    ParameterOverrideUVE overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterTypeUVE::Vector;
    overrideEntry.vector = value;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorldUVE::SetParameterColorUVE(const VfxHandleUVE handle,
                                       const std::string_view name,
                                       const Math::ColorUVE& color,
                                       const float alpha) {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr || !slot->instance.SetParameterColorUVE(name, color, alpha)) {
        return false;
    }
    ParameterOverrideUVE overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterTypeUVE::Color;
    overrideEntry.vector = Math::Vector3UVE{color.r, color.g, color.b};
    overrideEntry.alpha = alpha;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorldUVE::SetParameterBoolUVE(const VfxHandleUVE handle,
                                      const std::string_view name,
                                      const bool value) {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr || !slot->instance.SetParameterBoolUVE(name, value)) {
        return false;
    }
    ParameterOverrideUVE overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterTypeUVE::Bool;
    overrideEntry.vector.x = value ? 1.0F : 0.0F;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorldUVE::SetParameterIntUVE(const VfxHandleUVE handle,
                                     const std::string_view name,
                                     const std::int32_t value) {
    InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    if (slot == nullptr || !slot->instance.SetParameterIntUVE(name, value)) {
        return false;
    }
    ParameterOverrideUVE overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterTypeUVE::Int;
    overrideEntry.vector.x = static_cast<float>(value);
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

std::size_t VfxWorldUVE::PollHotReloadUVE() {
    std::size_t reloadedCount = 0U;
    const VfxCompileContextUVE compileContext{m_projectRoot};

    for (auto& [key, entry] : m_assetCache) {
        if (!entry.isDiskAsset || entry.resolvedDiskPath.empty()) {
            continue;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(entry.resolvedDiskPath, ec) || ec) {
            continue;
        }
        const auto currentWriteTime = std::filesystem::last_write_time(entry.resolvedDiskPath, ec);
        if (ec) {
            continue;
        }
        const auto currentFileSize = std::filesystem::file_size(entry.resolvedDiskPath, ec);
        if (ec) {
            continue;
        }
        if (currentWriteTime == entry.lastWriteTime && currentFileSize == entry.fileSize) {
            continue;
        }

        Asset::ParticleSystemAssetUVE newAsset;
        if (!Asset::LoadParticleSystemAssetUVE(entry.resolvedDiskPath, newAsset) ||
            !ValidateAndPrecompileAssetUVE(newAsset, compileContext)) {
            UVE_WARNING("VfxWorldUVE: hot reload of \"{}\" failed validation/compile; keeping previous version",
                        entry.resolvedDiskPath.string());
            continue;
        }

        entry.asset = std::move(newAsset);
        entry.lastWriteTime = currentWriteTime;
        entry.fileSize = currentFileSize;
        ++reloadedCount;
        ++m_stats.hotReloadCount;

        for (InstanceSlotUVE& slot : m_slots) {
            if (!slot.active || slot.cacheKey != key) {
                continue;
            }
            VfxOwnerUVE effectiveOwner = slot.owner;
            effectiveOwner.qualityLevel = m_qualityLevel;
            VfxInstanceUVE rebuilt;
            if (rebuilt.InitializeUVE(entry.asset, effectiveOwner, slot.instanceSeed, compileContext)) {
                ApplyParameterOverridesUVE(rebuilt, slot.parameterOverrides);
                slot.instance = std::move(rebuilt);
                slot.outOfFrustumFrameCounter = 0U;
                slot.accumulatedOutOfFrustumDt = 0.0F;
            }
        }
    }

    if (reloadedCount > 0U) {
        UpdateSummaryStatsUVE();
    }
    return reloadedCount;
}

void VfxWorldUVE::SetSceneCollidersUVE(std::vector<VfxColliderVolumeUVE> colliders) {
    m_sceneColliders = std::move(colliders);
}

void VfxWorldUVE::ClearSceneCollidersUVE() noexcept {
    m_sceneColliders.clear();
}

void VfxWorldUVE::TickUVE(const float dt, const VfxFrameViewUVE& frameView) {
    (void)PollHotReloadUVE();

    if (!std::isfinite(dt) || dt < 0.0F) {
        return;
    }

    std::size_t liveParticlesBeforeTick = 0U;
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        InstanceSlotUVE& slot = m_slots[idx];
        if (!slot.active) {
            continue;
        }
        if (!slot.instance.IsAliveUVE()) {
            RetireSlotUVE(idx, slot);
            continue;
        }
        liveParticlesBeforeTick += slot.instance.GetLiveCountUVE();
    }

    float budgetScale = 1.0F;
    if (m_globalBudget == 0U) {
        budgetScale = 0.0F;
    } else if (liveParticlesBeforeTick > m_globalBudget) {
        budgetScale = static_cast<float>(m_globalBudget) / static_cast<float>(liveParticlesBeforeTick);
    }
    m_stats.lastBudgetScale = budgetScale;

    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        InstanceSlotUVE& slot = m_slots[idx];
        if (!slot.active) {
            continue;
        }

        VfxOwnerUVE effectiveOwner = slot.owner;
        effectiveOwner.qualityLevel = m_qualityLevel;

        float instanceSpawnScale = budgetScale;
        const Asset::ParticleScalabilityUVE& scalability = slot.instance.GetScalabilityUVE(m_qualityLevel);
        if (frameView.hasCamera && scalability.cullDistance > 0.0F) {
            const float distSq = Math::LengthSquaredUVE(effectiveOwner.position - frameView.cameraPosition);
            const float cullDistSq = scalability.cullDistance * scalability.cullDistance;
            if (distSq > cullDistSq) {
                instanceSpawnScale = 0.0F;
                ++m_stats.distanceCulledSpawns;
            }
        }

        bool inFrustum = true;
        if (frameView.hasCamera) {
            const Math::Matrix4x4UVE ownerWorld = Math::Matrix4x4UVE::ComposeTrsUVE(
                effectiveOwner.position, effectiveOwner.rotation, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
            const Math::AabbUVE worldBounds = slot.instance.GetFixedBoundsUVE().TransformUVE(ownerWorld);
            inFrustum = frameView.frustum.IntersectsUVE(worldBounds);
        }

        if (inFrustum) {
            const float stepDt = dt + slot.accumulatedOutOfFrustumDt;
            slot.accumulatedOutOfFrustumDt = 0.0F;
            slot.outOfFrustumFrameCounter = 0U;
            if (stepDt > 0.0F) {
                AdvanceInstanceByTotalDtUVE(slot.instance, stepDt, effectiveOwner, instanceSpawnScale,
                                            m_sceneColliders);
            }
        } else {
            slot.accumulatedOutOfFrustumDt += dt;
            ++slot.outOfFrustumFrameCounter;
            if (slot.outOfFrustumFrameCounter >= 4U) {
                const float stepDt = slot.accumulatedOutOfFrustumDt;
                slot.accumulatedOutOfFrustumDt = 0.0F;
                slot.outOfFrustumFrameCounter = 0U;
                if (stepDt > 0.0F) {
                    AdvanceInstanceByTotalDtUVE(slot.instance, stepDt, effectiveOwner, instanceSpawnScale,
                                                m_sceneColliders);
                }
            } else {
                ++m_stats.quarterRateSkippedTicks;
            }
        }

        if (!slot.instance.IsAliveUVE()) {
            RetireSlotUVE(idx, slot);
        }
    }

    UpdateSummaryStatsUVE();
}

const VfxInstanceUVE* VfxWorldUVE::TryGetInstanceUVE(const VfxHandleUVE handle) const noexcept {
    const InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    return slot != nullptr ? &slot->instance : nullptr;
}

const VfxOwnerUVE* VfxWorldUVE::TryGetOwnerUVE(const VfxHandleUVE handle) const noexcept {
    const InstanceSlotUVE* const slot = ResolveSlotUVE(handle);
    return slot != nullptr ? &slot->owner : nullptr;
}

void VfxWorldUVE::CollectActiveInstancesUVE(std::vector<VfxActiveInstanceRefUVE>& out) const {
    out.clear();
    out.reserve(m_stats.activeInstanceCount);
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        const InstanceSlotUVE& slot = m_slots[idx];
        if (!slot.active || !slot.instance.IsAliveUVE()) {
            continue;
        }
        out.push_back(VfxActiveInstanceRefUVE{
            VfxHandleUVE{idx, slot.generation},
            &slot.instance,
            &slot.owner,
            slot.attachEntityKey,
        });
    }
}

VfxWorldUVE::InstanceSlotUVE* VfxWorldUVE::ResolveSlotUVE(const VfxHandleUVE handle) noexcept {
    if (!handle.IsValidUVE() || handle.index >= m_slots.size()) {
        return nullptr;
    }
    InstanceSlotUVE& slot = m_slots[handle.index];
    if (!slot.active || slot.generation != handle.generation) {
        return nullptr;
    }
    return &slot;
}

const VfxWorldUVE::InstanceSlotUVE* VfxWorldUVE::ResolveSlotUVE(const VfxHandleUVE handle) const noexcept {
    if (!handle.IsValidUVE() || handle.index >= m_slots.size()) {
        return nullptr;
    }
    const InstanceSlotUVE& slot = m_slots[handle.index];
    if (!slot.active || slot.generation != handle.generation) {
        return nullptr;
    }
    return &slot;
}

void VfxWorldUVE::RetireSlotUVE(const std::uint32_t slotIndex, InstanceSlotUVE& slot) noexcept {
    if (!slot.active) {
        return;
    }
    slot.active = false;
    slot.cacheKey.clear();
    slot.attachEntityKey = 0ULL;
    slot.outOfFrustumFrameCounter = 0U;
    slot.accumulatedOutOfFrustumDt = 0.0F;
    slot.parameterOverrides.clear();
    ++slot.generation;
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }
    m_freeSlotIndices.push_back(slotIndex);
}

VfxHandleUVE VfxWorldUVE::AllocateAndInitializeSlotUVE(const std::string& cacheKey,
                                                       const Asset::ParticleSystemAssetUVE& asset,
                                                       const VfxOwnerUVE& owner,
                                                       const std::uint64_t attachEntityKey,
                                                       const std::uint32_t instanceSeed) {
    VfxOwnerUVE effectiveOwner = owner;
    effectiveOwner.qualityLevel = m_qualityLevel;

    std::uint32_t seedToUse = instanceSeed;
    if (seedToUse == 0U) {
        seedToUse = m_nextAutoSeed++;
        if (m_nextAutoSeed == 0U) {
            m_nextAutoSeed = 1U;
        }
    }

    VfxInstanceUVE newInstance;
    const VfxCompileContextUVE compileContext{m_projectRoot};
    if (!newInstance.InitializeUVE(asset, effectiveOwner, seedToUse, compileContext)) {
        return kInvalidVfxHandleUVE;
    }

    std::uint32_t slotIndex = 0U;
    if (!m_freeSlotIndices.empty()) {
        slotIndex = m_freeSlotIndices.back();
        m_freeSlotIndices.pop_back();
    } else {
        slotIndex = static_cast<std::uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    InstanceSlotUVE& slot = m_slots[slotIndex];
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }
    slot.active = true;
    slot.cacheKey = cacheKey;
    slot.owner = effectiveOwner;
    slot.attachEntityKey = attachEntityKey;
    slot.instanceSeed = seedToUse;
    slot.outOfFrustumFrameCounter = 0U;
    slot.accumulatedOutOfFrustumDt = 0.0F;
    slot.parameterOverrides.clear();
    slot.instance = std::move(newInstance);

    UpdateSummaryStatsUVE();
    return VfxHandleUVE{slotIndex, slot.generation};
}

void VfxWorldUVE::ApplyParameterOverridesUVE(
    VfxInstanceUVE& instance,
    const std::map<std::string, ParameterOverrideUVE, std::less<>>& overrides) noexcept {
    for (const auto& [name, entry] : overrides) {
        switch (entry.type) {
            case Asset::ParticleUserParameterTypeUVE::Float:
                (void)instance.SetParameterFloatUVE(name, entry.vector.x);
                break;
            case Asset::ParticleUserParameterTypeUVE::Vector:
                (void)instance.SetParameterVectorUVE(name, entry.vector);
                break;
            case Asset::ParticleUserParameterTypeUVE::Color:
                (void)instance.SetParameterColorUVE(
                    name, Math::ColorUVE{entry.vector.x, entry.vector.y, entry.vector.z}, entry.alpha);
                break;
            case Asset::ParticleUserParameterTypeUVE::Bool:
                (void)instance.SetParameterBoolUVE(name, entry.vector.x >= 0.5F);
                break;
            case Asset::ParticleUserParameterTypeUVE::Int:
                (void)instance.SetParameterIntUVE(name, static_cast<std::int32_t>(std::lround(entry.vector.x)));
                break;
        }
    }
}

void VfxWorldUVE::UpdateSummaryStatsUVE() noexcept {
    std::size_t activeCount = 0U;
    std::size_t totalLive = 0U;
    for (const InstanceSlotUVE& slot : m_slots) {
        if (!slot.active) {
            continue;
        }
        ++activeCount;
        totalLive += slot.instance.GetLiveCountUVE();
    }
    m_stats.activeInstanceCount = activeCount;
    m_stats.cachedAssetCount = m_assetCache.size();
    m_stats.totalLiveParticles = totalLive;
    m_stats.globalBudget = m_globalBudget;
}

} // namespace UVE::VFX
