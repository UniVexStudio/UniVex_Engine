// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "color.h"
#include "frustum.h"
#include "particle_system_asset.h"
#include "vector3.h"
#include "vfx_instance.h"
#include "vfx_values.h"

namespace UVE::VFX {

inline constexpr std::size_t kDefaultVfxGlobalBudgetUVE = 200'000U;

/// Generational handle identifying a playing particle system instance owned by VfxWorldUVE.
struct VfxHandleUVE final {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;

    [[nodiscard]] constexpr bool IsValidUVE() const noexcept { return generation != 0U; }

    [[nodiscard]] constexpr std::int64_t ToPackedInt64UVE() const noexcept {
        const std::uint64_t packed =
            (static_cast<std::uint64_t>(generation) << 32ULL) | static_cast<std::uint64_t>(index);
        return static_cast<std::int64_t>(packed);
    }

    [[nodiscard]] static constexpr VfxHandleUVE FromPackedInt64UVE(const std::int64_t packed) noexcept {
        const auto raw = static_cast<std::uint64_t>(packed);
        return VfxHandleUVE{
            static_cast<std::uint32_t>(raw & 0xFFFFFFFFULL),
            static_cast<std::uint32_t>((raw >> 32ULL) & 0xFFFFFFFFULL),
        };
    }

    [[nodiscard]] constexpr bool operator==(const VfxHandleUVE&) const noexcept = default;
};

inline constexpr VfxHandleUVE kInvalidVfxHandleUVE{};

/// Active camera view information supplied to VfxWorldUVE::TickUVE for distance LOD and frustum culling.
struct VfxFrameViewUVE final {
    bool hasCamera = false;
    Math::Vector3UVE cameraPosition{};
    Math::FrustumUVE frustum{};
};

/// Diagnostic counters reported by VfxWorldUVE.
struct VfxWorldStatsUVE final {
    std::size_t activeInstanceCount = 0U;
    std::size_t cachedAssetCount = 0U;
    std::size_t totalLiveParticles = 0U;
    std::size_t globalBudget = kDefaultVfxGlobalBudgetUVE;
    float lastBudgetScale = 1.0F;
    std::uint64_t hotReloadCount = 0U;
    std::uint64_t quarterRateSkippedTicks = 0U;
    std::uint64_t distanceCulledSpawns = 0U;
};

/// Read-only view into an active playing instance for render-bridge extraction.
struct VfxActiveInstanceRefUVE final {
    VfxHandleUVE handle{};
    const VfxInstanceUVE* instance = nullptr;
    const VfxOwnerUVE* owner = nullptr;
    std::uint64_t attachEntityKey = 0ULL;
};

/// Owns all active particle system instances, path-keyed asset caching, hot reload, quality level,
/// global particle budget scaling, distance LOD, and out-of-frustum quarter-rate ticking.
class VfxWorldUVE final {
public:
    VfxWorldUVE();
    ~VfxWorldUVE() = default;

    VfxWorldUVE(const VfxWorldUVE&) = delete;
    VfxWorldUVE& operator=(const VfxWorldUVE&) = delete;
    VfxWorldUVE(VfxWorldUVE&&) noexcept = default;
    VfxWorldUVE& operator=(VfxWorldUVE&&) noexcept = default;

    void SetProjectRootUVE(const std::filesystem::path& projectRoot);
    [[nodiscard]] const std::filesystem::path& GetProjectRootUVE() const noexcept { return m_projectRoot; }

    void SetGlobalParticleBudgetUVE(std::size_t maxLiveParticles) noexcept;
    [[nodiscard]] std::size_t GetGlobalParticleBudgetUVE() const noexcept { return m_globalBudget; }

    void SetQualityLevelUVE(std::uint8_t level) noexcept;
    [[nodiscard]] std::uint8_t GetQualityLevelUVE() const noexcept { return m_qualityLevel; }

    void ClearCacheUVE();

    /// Resolves `assetPath` against `projectRoot` (`root/`, `root/content/`, `root/assets/`) or CWD.
    /// Returns an empty path if no regular file is found.
    [[nodiscard]] std::filesystem::path ResolveAssetPathUVE(std::string_view assetPath) const;

    /// Loads (or reuses cached) `.uvfx` asset from disk, compiles and spawns a playing instance.
    [[nodiscard]] VfxHandleUVE SpawnUVE(std::string_view assetPath,
                                        const VfxOwnerUVE& owner = {},
                                        std::uint64_t attachEntityKey = 0ULL,
                                        std::uint32_t instanceSeed = 0U);

    /// Spawns an instance directly from an in-memory system asset (cached under `cacheKey` if non-empty).
    [[nodiscard]] VfxHandleUVE SpawnFromAssetUVE(std::string_view cacheKey,
                                                 const Asset::ParticleSystemAssetUVE& system,
                                                 const VfxOwnerUVE& owner = {},
                                                 std::uint64_t attachEntityKey = 0ULL,
                                                 std::uint32_t instanceSeed = 0U);

    bool StopUVE(VfxHandleUVE handle) noexcept;
    bool KillUVE(VfxHandleUVE handle) noexcept;
    void ClearAllInstancesUVE() noexcept;

    [[nodiscard]] bool IsAliveUVE(VfxHandleUVE handle) const noexcept;
    bool SetOwnerUVE(VfxHandleUVE handle, const VfxOwnerUVE& owner) noexcept;

    bool SetParameterFloatUVE(VfxHandleUVE handle, std::string_view name, float value);
    bool SetParameterVectorUVE(VfxHandleUVE handle, std::string_view name, const Math::Vector3UVE& value);
    bool SetParameterColorUVE(VfxHandleUVE handle, std::string_view name, const Math::ColorUVE& color,
                              float alpha = 1.0F);
    bool SetParameterBoolUVE(VfxHandleUVE handle, std::string_view name, bool value);
    bool SetParameterIntUVE(VfxHandleUVE handle, std::string_view name, std::int32_t value);

    /// Checks disk modification times on all cached `.uvfx` assets; reloads, validates, recompiles,
    /// and restarts active instances using any modified asset. Invalid reloads keep old instances.
    std::size_t PollHotReloadUVE();

    /// Advances active instances by `dt` seconds with budget scaling, distance LOD, and frustum
    /// quarter-rate accumulation.
    void TickUVE(float dt, const VfxFrameViewUVE& frameView = {});

    [[nodiscard]] const VfxInstanceUVE* TryGetInstanceUVE(VfxHandleUVE handle) const noexcept;
    [[nodiscard]] const VfxOwnerUVE* TryGetOwnerUVE(VfxHandleUVE handle) const noexcept;

    void CollectActiveInstancesUVE(std::vector<VfxActiveInstanceRefUVE>& out) const;
    [[nodiscard]] const VfxWorldStatsUVE& GetStatsUVE() const noexcept { return m_stats; }

private:
    struct CachedAssetEntryUVE final {
        std::filesystem::path resolvedDiskPath;
        bool isDiskAsset = false;
        std::filesystem::file_time_type lastWriteTime{};
        std::uintmax_t fileSize = 0U;
        Asset::ParticleSystemAssetUVE asset;
    };

    struct ParameterOverrideUVE final {
        Asset::ParticleUserParameterTypeUVE type = Asset::ParticleUserParameterTypeUVE::Float;
        Math::Vector3UVE vector{};
        float alpha = 1.0F;
    };

    struct InstanceSlotUVE final {
        std::uint32_t generation = 1U;
        bool active = false;
        std::string cacheKey;
        VfxOwnerUVE owner{};
        std::uint64_t attachEntityKey = 0ULL;
        std::uint32_t instanceSeed = 1U;
        std::uint32_t outOfFrustumFrameCounter = 0U;
        float accumulatedOutOfFrustumDt = 0.0F;
        std::map<std::string, ParameterOverrideUVE, std::less<>> parameterOverrides;
        VfxInstanceUVE instance;
    };

    [[nodiscard]] InstanceSlotUVE* ResolveSlotUVE(VfxHandleUVE handle) noexcept;
    [[nodiscard]] const InstanceSlotUVE* ResolveSlotUVE(VfxHandleUVE handle) const noexcept;
    void RetireSlotUVE(std::uint32_t slotIndex, InstanceSlotUVE& slot) noexcept;
    [[nodiscard]] VfxHandleUVE AllocateAndInitializeSlotUVE(const std::string& cacheKey,
                                                            const Asset::ParticleSystemAssetUVE& asset,
                                                            const VfxOwnerUVE& owner,
                                                            std::uint64_t attachEntityKey,
                                                            std::uint32_t instanceSeed);
    static void ApplyParameterOverridesUVE(
        VfxInstanceUVE& instance,
        const std::map<std::string, ParameterOverrideUVE, std::less<>>& overrides) noexcept;
    void UpdateSummaryStatsUVE() noexcept;

    std::filesystem::path m_projectRoot;
    std::size_t m_globalBudget = kDefaultVfxGlobalBudgetUVE;
    std::uint8_t m_qualityLevel = 2U;
    std::uint32_t m_nextAutoSeed = 1U;
    std::unordered_map<std::string, CachedAssetEntryUVE> m_assetCache;
    std::vector<InstanceSlotUVE> m_slots;
    std::vector<std::uint32_t> m_freeSlotIndices;
    VfxWorldStatsUVE m_stats{};
};

} // namespace UVE::VFX
