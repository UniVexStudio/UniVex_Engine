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

constexpr float kMaxInstanceStepDt = 0.1F;

[[nodiscard]] bool IsSafeAssetQueryPath(const std::string_view assetPath) noexcept {
    if (assetPath.empty() || assetPath.find('\0') != std::string_view::npos) {
        return false;
    }
    return true;
}

[[nodiscard]] std::string NormalizeCacheKey(const std::filesystem::path& resolvedPath) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(resolvedPath, ec);
    if (!ec && !canonical.empty()) {
        return canonical.generic_string();
    }
    return resolvedPath.lexically_normal().generic_string();
}

void AdvanceInstanceByTotalDt(VfxInstance& instance,
                                 float totalDt,
                                 const VfxOwner& owner,
                                 const float externalSpawnScale,
                                 const std::span<const VfxColliderVolume> sceneColliders) {
    while (totalDt > 1e-6F) {
        const float stepDt = std::min(totalDt, kMaxInstanceStepDt);
        instance.Tick(stepDt, owner, externalSpawnScale, sceneColliders);
        totalDt -= stepDt;
    }
}

[[nodiscard]] bool ValidateAndPrecompileAsset(const Asset::ParticleSystemAsset& asset,
                                                 const VfxCompileContext& compileContext) {
    if (!Asset::IsParticleSystemAssetValid(asset)) {
        return false;
    }
    std::vector<ParticleCompileMessage> messages;
    for (std::size_t idx = 0U; idx < asset.emitters.size(); ++idx) {
        ParticleProgram program;
        if (!CompileParticleEmitter(asset, idx, compileContext, program, messages)) {
            return false;
        }
    }
    return true;
}

} // namespace

VfxWorld::VfxWorld() {
#if defined(__ANDROID__)
    m_qualityLevel = 0U;
#else
    m_qualityLevel = 2U;
#endif
    m_stats.globalBudget = m_globalBudget;
}

void VfxWorld::SetProjectRoot(const std::filesystem::path& projectRoot) {
    m_projectRoot = projectRoot;
}

void VfxWorld::SetGlobalParticleBudget(const std::size_t maxLiveParticles) noexcept {
    m_globalBudget = maxLiveParticles;
    m_stats.globalBudget = maxLiveParticles;
}

void VfxWorld::SetQualityLevel(const std::uint8_t level) noexcept {
    m_qualityLevel = std::min<std::uint8_t>(level, 2U);
}

void VfxWorld::ClearCache() {
    m_assetCache.clear();
    UpdateSummaryStats();
}

std::filesystem::path VfxWorld::ResolveAssetPath(const std::string_view assetPath) const {
    if (!IsSafeAssetQueryPath(assetPath)) {
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

VfxHandle VfxWorld::Spawn(const std::string_view assetPath,
                                   const VfxOwner& owner,
                                   const std::uint64_t attachEntityKey,
                                   const std::uint32_t instanceSeed) {
    const std::filesystem::path resolved = ResolveAssetPath(assetPath);
    if (resolved.empty()) {
        UVE_WARNING("VfxWorld: could not resolve particle system asset path \"{}\"", assetPath);
        return kInvalidVfxHandle;
    }

    const std::string cacheKey = NormalizeCacheKey(resolved);
    auto cacheIt = m_assetCache.find(cacheKey);
    if (cacheIt == m_assetCache.end()) {
        Asset::ParticleSystemAsset loadedAsset;
        if (!Asset::LoadParticleSystemAsset(resolved, loadedAsset)) {
            UVE_WARNING("VfxWorld: failed to load particle system asset \"{}\"", resolved.string());
            return kInvalidVfxHandle;
        }
        const VfxCompileContext compileContext{m_projectRoot};
        if (!ValidateAndPrecompileAsset(loadedAsset, compileContext)) {
            UVE_WARNING("VfxWorld: failed to compile particle system asset \"{}\"", resolved.string());
            return kInvalidVfxHandle;
        }

        std::error_code ec;
        CachedAssetEntry entry;
        entry.resolvedDiskPath = resolved;
        entry.isDiskAsset = true;
        entry.lastWriteTime = std::filesystem::last_write_time(resolved, ec);
        entry.fileSize = ec ? 0U : std::filesystem::file_size(resolved, ec);
        entry.asset = std::move(loadedAsset);
        cacheIt = m_assetCache.emplace(cacheKey, std::move(entry)).first;
    }

    return AllocateAndInitializeSlot(cacheKey, cacheIt->second.asset, owner, attachEntityKey, instanceSeed);
}

VfxHandle VfxWorld::SpawnFromAsset(const std::string_view cacheKey,
                                            const Asset::ParticleSystemAsset& system,
                                            const VfxOwner& owner,
                                            const std::uint64_t attachEntityKey,
                                            const std::uint32_t instanceSeed) {
    std::string normalizedKey;
    if (!cacheKey.empty()) {
        normalizedKey.assign(cacheKey);
        auto it = m_assetCache.find(normalizedKey);
        if (it == m_assetCache.end()) {
            const VfxCompileContext compileContext{m_projectRoot};
            if (!ValidateAndPrecompileAsset(system, compileContext)) {
                return kInvalidVfxHandle;
            }
            CachedAssetEntry entry;
            entry.isDiskAsset = false;
            entry.asset = system;
            m_assetCache.emplace(normalizedKey, std::move(entry));
        }
    }
    return AllocateAndInitializeSlot(normalizedKey, system, owner, attachEntityKey, instanceSeed);
}

bool VfxWorld::Stop(const VfxHandle handle) noexcept {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->instance.Stop();
    if (!slot->instance.IsAlive()) {
        RetireSlot(handle.index, *slot);
        UpdateSummaryStats();
    }
    return true;
}

bool VfxWorld::Kill(const VfxHandle handle) noexcept {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->instance.Kill();
    RetireSlot(handle.index, *slot);
    UpdateSummaryStats();
    return true;
}

void VfxWorld::ClearAllInstances() noexcept {
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        if (m_slots[idx].active) {
            m_slots[idx].instance.Kill();
            RetireSlot(idx, m_slots[idx]);
        }
    }
    UpdateSummaryStats();
}

bool VfxWorld::IsAlive(const VfxHandle handle) const noexcept {
    const InstanceSlot* const slot = ResolveSlot(handle);
    return slot != nullptr && slot->instance.IsAlive();
}

bool VfxWorld::SetOwner(const VfxHandle handle, const VfxOwner& owner) noexcept {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->owner = owner;
    slot->owner.qualityLevel = m_qualityLevel;
    return true;
}

bool VfxWorld::SetParameterFloat(const VfxHandle handle,
                                       const std::string_view name,
                                       const float value) {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr || !slot->instance.SetParameterFloat(name, value)) {
        return false;
    }
    ParameterOverride overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterType::Float;
    overrideEntry.vector.x = value;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorld::SetParameterVector(const VfxHandle handle,
                                        const std::string_view name,
                                        const Math::Vector3& value) {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr || !slot->instance.SetParameterVector(name, value)) {
        return false;
    }
    ParameterOverride overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterType::Vector;
    overrideEntry.vector = value;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorld::SetParameterColor(const VfxHandle handle,
                                       const std::string_view name,
                                       const Math::Color& color,
                                       const float alpha) {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr || !slot->instance.SetParameterColor(name, color, alpha)) {
        return false;
    }
    ParameterOverride overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterType::Color;
    overrideEntry.vector = Math::Vector3{color.r, color.g, color.b};
    overrideEntry.alpha = alpha;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorld::SetParameterBool(const VfxHandle handle,
                                      const std::string_view name,
                                      const bool value) {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr || !slot->instance.SetParameterBool(name, value)) {
        return false;
    }
    ParameterOverride overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterType::Bool;
    overrideEntry.vector.x = value ? 1.0F : 0.0F;
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

bool VfxWorld::SetParameterInt(const VfxHandle handle,
                                     const std::string_view name,
                                     const std::int32_t value) {
    InstanceSlot* const slot = ResolveSlot(handle);
    if (slot == nullptr || !slot->instance.SetParameterInt(name, value)) {
        return false;
    }
    ParameterOverride overrideEntry;
    overrideEntry.type = Asset::ParticleUserParameterType::Int;
    overrideEntry.vector.x = static_cast<float>(value);
    slot->parameterOverrides[std::string(name)] = overrideEntry;
    return true;
}

std::size_t VfxWorld::PollHotReload() {
    std::size_t reloadedCount = 0U;
    const VfxCompileContext compileContext{m_projectRoot};

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

        Asset::ParticleSystemAsset newAsset;
        if (!Asset::LoadParticleSystemAsset(entry.resolvedDiskPath, newAsset) ||
            !ValidateAndPrecompileAsset(newAsset, compileContext)) {
            UVE_WARNING("VfxWorld: hot reload of \"{}\" failed validation/compile; keeping previous version",
                        entry.resolvedDiskPath.string());
            continue;
        }

        entry.asset = std::move(newAsset);
        entry.lastWriteTime = currentWriteTime;
        entry.fileSize = currentFileSize;
        ++reloadedCount;
        ++m_stats.hotReloadCount;

        for (InstanceSlot& slot : m_slots) {
            if (!slot.active || slot.cacheKey != key) {
                continue;
            }
            VfxOwner effectiveOwner = slot.owner;
            effectiveOwner.qualityLevel = m_qualityLevel;
            VfxInstance rebuilt;
            if (rebuilt.Initialize(entry.asset, effectiveOwner, slot.instanceSeed, compileContext)) {
                ApplyParameterOverrides(rebuilt, slot.parameterOverrides);
                slot.instance = std::move(rebuilt);
                slot.outOfFrustumFrameCounter = 0U;
                slot.accumulatedOutOfFrustumDt = 0.0F;
            }
        }
    }

    if (reloadedCount > 0U) {
        UpdateSummaryStats();
    }
    return reloadedCount;
}

void VfxWorld::SetSceneColliders(std::vector<VfxColliderVolume> colliders) {
    m_sceneColliders = std::move(colliders);
}

void VfxWorld::ClearSceneColliders() noexcept {
    m_sceneColliders.clear();
}

void VfxWorld::Tick(const float dt, const VfxFrameView& frameView) {
    if (!std::isfinite(dt) || dt < 0.0F) {
        return;
    }
    if (!m_assetCache.empty()) {
        constexpr float kHotReloadPollIntervalSeconds = 0.5F;
        m_hotReloadPollAccumulator += dt;
        if (m_hotReloadPollAccumulator >= kHotReloadPollIntervalSeconds) {
            m_hotReloadPollAccumulator = 0.0F;
            (void)PollHotReload();
        }
    }

    std::size_t liveParticlesBeforeTick = 0U;
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        InstanceSlot& slot = m_slots[idx];
        if (!slot.active) {
            continue;
        }
        if (!slot.instance.IsAlive()) {
            RetireSlot(idx, slot);
            continue;
        }
        liveParticlesBeforeTick += slot.instance.GetLiveCount();
    }

    float budgetScale = 1.0F;
    if (m_globalBudget == 0U) {
        budgetScale = 0.0F;
    } else if (liveParticlesBeforeTick > m_globalBudget) {
        budgetScale = static_cast<float>(m_globalBudget) / static_cast<float>(liveParticlesBeforeTick);
    }
    m_stats.lastBudgetScale = budgetScale;

    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        InstanceSlot& slot = m_slots[idx];
        if (!slot.active) {
            continue;
        }

        VfxOwner effectiveOwner = slot.owner;
        effectiveOwner.qualityLevel = m_qualityLevel;

        float instanceSpawnScale = budgetScale;
        const Asset::ParticleScalability& scalability = slot.instance.GetScalability(m_qualityLevel);
        if (frameView.hasCamera && scalability.cullDistance > 0.0F) {
            const float distSq = Math::LengthSquared(effectiveOwner.position - frameView.cameraPosition);
            const float cullDistSq = scalability.cullDistance * scalability.cullDistance;
            if (distSq > cullDistSq) {
                instanceSpawnScale = 0.0F;
                ++m_stats.distanceCulledSpawns;
            }
        }

        bool inFrustum = true;
        if (frameView.hasCamera) {
            const Math::Matrix4x4 ownerWorld = Math::Matrix4x4::ComposeTrs(
                effectiveOwner.position, effectiveOwner.rotation, Math::Vector3{1.0F, 1.0F, 1.0F});
            const Math::Aabb worldBounds = slot.instance.GetFixedBounds().Transform(ownerWorld);
            inFrustum = frameView.frustum.Intersects(worldBounds);
        }

        if (inFrustum) {
            const float stepDt = dt + slot.accumulatedOutOfFrustumDt;
            slot.accumulatedOutOfFrustumDt = 0.0F;
            slot.outOfFrustumFrameCounter = 0U;
            if (stepDt > 0.0F) {
                AdvanceInstanceByTotalDt(slot.instance, stepDt, effectiveOwner, instanceSpawnScale,
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
                    AdvanceInstanceByTotalDt(slot.instance, stepDt, effectiveOwner, instanceSpawnScale,
                                                m_sceneColliders);
                }
            } else {
                ++m_stats.quarterRateSkippedTicks;
            }
        }

        if (!slot.instance.IsAlive()) {
            RetireSlot(idx, slot);
        }
    }

    UpdateSummaryStats();
}

const VfxInstance* VfxWorld::TryGetInstance(const VfxHandle handle) const noexcept {
    const InstanceSlot* const slot = ResolveSlot(handle);
    return slot != nullptr ? &slot->instance : nullptr;
}

const VfxOwner* VfxWorld::TryGetOwner(const VfxHandle handle) const noexcept {
    const InstanceSlot* const slot = ResolveSlot(handle);
    return slot != nullptr ? &slot->owner : nullptr;
}

void VfxWorld::CollectActiveInstances(std::vector<VfxActiveInstanceRef>& out) const {
    out.clear();
    out.reserve(m_stats.activeInstanceCount);
    for (std::uint32_t idx = 0U; idx < static_cast<std::uint32_t>(m_slots.size()); ++idx) {
        const InstanceSlot& slot = m_slots[idx];
        if (!slot.active || !slot.instance.IsAlive()) {
            continue;
        }
        out.push_back(VfxActiveInstanceRef{
            VfxHandle{idx, slot.generation},
            &slot.instance,
            &slot.owner,
            slot.attachEntityKey,
        });
    }
}

VfxWorld::InstanceSlot* VfxWorld::ResolveSlot(const VfxHandle handle) noexcept {
    if (!handle.IsValid() || handle.index >= m_slots.size()) {
        return nullptr;
    }
    InstanceSlot& slot = m_slots[handle.index];
    if (!slot.active || slot.generation != handle.generation) {
        return nullptr;
    }
    return &slot;
}

const VfxWorld::InstanceSlot* VfxWorld::ResolveSlot(const VfxHandle handle) const noexcept {
    if (!handle.IsValid() || handle.index >= m_slots.size()) {
        return nullptr;
    }
    const InstanceSlot& slot = m_slots[handle.index];
    if (!slot.active || slot.generation != handle.generation) {
        return nullptr;
    }
    return &slot;
}

void VfxWorld::RetireSlot(const std::uint32_t slotIndex, InstanceSlot& slot) noexcept {
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

VfxHandle VfxWorld::AllocateAndInitializeSlot(const std::string& cacheKey,
                                                       const Asset::ParticleSystemAsset& asset,
                                                       const VfxOwner& owner,
                                                       const std::uint64_t attachEntityKey,
                                                       const std::uint32_t instanceSeed) {
    VfxOwner effectiveOwner = owner;
    effectiveOwner.qualityLevel = m_qualityLevel;

    std::uint32_t seedToUse = instanceSeed;
    if (seedToUse == 0U) {
        seedToUse = m_nextAutoSeed++;
        if (m_nextAutoSeed == 0U) {
            m_nextAutoSeed = 1U;
        }
    }

    VfxInstance newInstance;
    const VfxCompileContext compileContext{m_projectRoot};
    if (!newInstance.Initialize(asset, effectiveOwner, seedToUse, compileContext)) {
        return kInvalidVfxHandle;
    }

    std::uint32_t slotIndex = 0U;
    if (!m_freeSlotIndices.empty()) {
        slotIndex = m_freeSlotIndices.back();
        m_freeSlotIndices.pop_back();
    } else {
        slotIndex = static_cast<std::uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    InstanceSlot& slot = m_slots[slotIndex];
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

    UpdateSummaryStats();
    return VfxHandle{slotIndex, slot.generation};
}

void VfxWorld::ApplyParameterOverrides(
    VfxInstance& instance,
    const std::map<std::string, ParameterOverride, std::less<>>& overrides) noexcept {
    for (const auto& [name, entry] : overrides) {
        switch (entry.type) {
            case Asset::ParticleUserParameterType::Float:
                (void)instance.SetParameterFloat(name, entry.vector.x);
                break;
            case Asset::ParticleUserParameterType::Vector:
                (void)instance.SetParameterVector(name, entry.vector);
                break;
            case Asset::ParticleUserParameterType::Color:
                (void)instance.SetParameterColor(
                    name, Math::Color{entry.vector.x, entry.vector.y, entry.vector.z}, entry.alpha);
                break;
            case Asset::ParticleUserParameterType::Bool:
                (void)instance.SetParameterBool(name, entry.vector.x >= 0.5F);
                break;
            case Asset::ParticleUserParameterType::Int:
                (void)instance.SetParameterInt(name, static_cast<std::int32_t>(std::lround(entry.vector.x)));
                break;
        }
    }
}

void VfxWorld::UpdateSummaryStats() noexcept {
    std::size_t activeCount = 0U;
    std::size_t totalLive = 0U;
    for (const InstanceSlot& slot : m_slots) {
        if (!slot.active) {
            continue;
        }
        ++activeCount;
        totalLive += slot.instance.GetLiveCount();
    }
    m_stats.activeInstanceCount = activeCount;
    m_stats.cachedAssetCount = m_assetCache.size();
    m_stats.totalLiveParticles = totalLive;
    m_stats.globalBudget = m_globalBudget;
}

} // namespace UVE::VFX
