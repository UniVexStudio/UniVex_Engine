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

inline constexpr std::size_t kDefaultVfxGlobalBudget = 200'000U;

/// Generational handle identifying a playing particle system instance owned by VfxWorld.
struct VfxHandle final {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0U; }

    [[nodiscard]] constexpr std::int64_t ToPackedInt64() const noexcept {
        const std::uint64_t packed =
            (static_cast<std::uint64_t>(generation) << 32ULL) | static_cast<std::uint64_t>(index);
        return static_cast<std::int64_t>(packed);
    }

    [[nodiscard]] static constexpr VfxHandle FromPackedInt64(const std::int64_t packed) noexcept {
        const auto raw = static_cast<std::uint64_t>(packed);
        return VfxHandle{
            static_cast<std::uint32_t>(raw & 0xFFFFFFFFULL),
            static_cast<std::uint32_t>((raw >> 32ULL) & 0xFFFFFFFFULL),
        };
    }

    [[nodiscard]] constexpr bool operator==(const VfxHandle&) const noexcept = default;
};

inline constexpr VfxHandle kInvalidVfxHandle{};

/// Active camera view information supplied to VfxWorld::Tick for distance LOD and frustum culling.
struct VfxFrameView final {
    bool hasCamera = false;
    Math::Vector3 cameraPosition{};
    Math::Frustum frustum{};
};

/// Diagnostic counters reported by VfxWorld.
struct VfxWorldStats final {
    std::size_t activeInstanceCount = 0U;
    std::size_t cachedAssetCount = 0U;
    std::size_t totalLiveParticles = 0U;
    std::size_t globalBudget = kDefaultVfxGlobalBudget;
    float lastBudgetScale = 1.0F;
    std::uint64_t hotReloadCount = 0U;
    std::uint64_t quarterRateSkippedTicks = 0U;
    std::uint64_t distanceCulledSpawns = 0U;
};

/// Read-only view into an active playing instance for render-bridge extraction.
struct VfxActiveInstanceRef final {
    VfxHandle handle{};
    const VfxInstance* instance = nullptr;
    const VfxOwner* owner = nullptr;
    std::uint64_t attachEntityKey = 0ULL;
};

/// Owns all active particle system instances, path-keyed asset caching, hot reload, quality level,
/// global particle budget scaling, distance LOD, and out-of-frustum quarter-rate ticking.
class VfxWorld final {
public:
    VfxWorld();
    ~VfxWorld() = default;

    VfxWorld(const VfxWorld&) = delete;
    VfxWorld& operator=(const VfxWorld&) = delete;
    VfxWorld(VfxWorld&&) noexcept = default;
    VfxWorld& operator=(VfxWorld&&) noexcept = default;

    void SetProjectRoot(const std::filesystem::path& projectRoot);
    [[nodiscard]] const std::filesystem::path& GetProjectRoot() const noexcept { return m_projectRoot; }

    void SetGlobalParticleBudget(std::size_t maxLiveParticles) noexcept;
    [[nodiscard]] std::size_t GetGlobalParticleBudget() const noexcept { return m_globalBudget; }

    void SetQualityLevel(std::uint8_t level) noexcept;
    [[nodiscard]] std::uint8_t GetQualityLevel() const noexcept { return m_qualityLevel; }

    void ClearCache();

    /// Resolves `assetPath` against `projectRoot` (`root/`, `root/content/`, `root/assets/`) or CWD.
    /// Returns an empty path if no regular file is found.
    [[nodiscard]] std::filesystem::path ResolveAssetPath(std::string_view assetPath) const;

    /// Loads (or reuses cached) `.uvfx` asset from disk, compiles and spawns a playing instance.
    [[nodiscard]] VfxHandle Spawn(std::string_view assetPath,
                                        const VfxOwner& owner = {},
                                        std::uint64_t attachEntityKey = 0ULL,
                                        std::uint32_t instanceSeed = 0U);

    /// Spawns an instance directly from an in-memory system asset (cached under `cacheKey` if non-empty).
    [[nodiscard]] VfxHandle SpawnFromAsset(std::string_view cacheKey,
                                                 const Asset::ParticleSystemAsset& system,
                                                 const VfxOwner& owner = {},
                                                 std::uint64_t attachEntityKey = 0ULL,
                                                 std::uint32_t instanceSeed = 0U);

    bool Stop(VfxHandle handle) noexcept;
    bool Kill(VfxHandle handle) noexcept;
    void ClearAllInstances() noexcept;

    [[nodiscard]] bool IsAlive(VfxHandle handle) const noexcept;
    bool SetOwner(VfxHandle handle, const VfxOwner& owner) noexcept;

    bool SetParameterFloat(VfxHandle handle, std::string_view name, float value);
    bool SetParameterVector(VfxHandle handle, std::string_view name, const Math::Vector3& value);
    bool SetParameterColor(VfxHandle handle, std::string_view name, const Math::Color& color,
                              float alpha = 1.0F);
    bool SetParameterBool(VfxHandle handle, std::string_view name, bool value);
    bool SetParameterInt(VfxHandle handle, std::string_view name, std::int32_t value);

    /// Checks disk modification times on all cached `.uvfx` assets; reloads, validates, recompiles,
    /// and restarts active instances using any modified asset. Invalid reloads keep old instances.
    std::size_t PollHotReload();

    /// Supplies the current frame's world-space Collider3D volumes for real-time particle collision.
    void SetSceneColliders(std::vector<VfxColliderVolume> colliders);
    void ClearSceneColliders() noexcept;

    /// Advances active instances by `dt` seconds with budget scaling, distance LOD, and frustum
    /// quarter-rate accumulation.
    void Tick(float dt, const VfxFrameView& frameView = {});

    [[nodiscard]] const VfxInstance* TryGetInstance(VfxHandle handle) const noexcept;
    [[nodiscard]] const VfxOwner* TryGetOwner(VfxHandle handle) const noexcept;

    void CollectActiveInstances(std::vector<VfxActiveInstanceRef>& out) const;
    [[nodiscard]] const VfxWorldStats& GetStats() const noexcept { return m_stats; }

private:
    struct CachedAssetEntry final {
        std::filesystem::path resolvedDiskPath;
        bool isDiskAsset = false;
        std::filesystem::file_time_type lastWriteTime{};
        std::uintmax_t fileSize = 0U;
        Asset::ParticleSystemAsset asset;
    };

    struct ParameterOverride final {
        Asset::ParticleUserParameterType type = Asset::ParticleUserParameterType::Float;
        Math::Vector3 vector{};
        float alpha = 1.0F;
    };

    struct InstanceSlot final {
        std::uint32_t generation = 1U;
        bool active = false;
        std::string cacheKey;
        VfxOwner owner{};
        std::uint64_t attachEntityKey = 0ULL;
        std::uint32_t instanceSeed = 1U;
        std::uint32_t outOfFrustumFrameCounter = 0U;
        float accumulatedOutOfFrustumDt = 0.0F;
        std::map<std::string, ParameterOverride, std::less<>> parameterOverrides;
        VfxInstance instance;
    };

    [[nodiscard]] InstanceSlot* ResolveSlot(VfxHandle handle) noexcept;
    [[nodiscard]] const InstanceSlot* ResolveSlot(VfxHandle handle) const noexcept;
    void RetireSlot(std::uint32_t slotIndex, InstanceSlot& slot) noexcept;
    [[nodiscard]] VfxHandle AllocateAndInitializeSlot(const std::string& cacheKey,
                                                            const Asset::ParticleSystemAsset& asset,
                                                            const VfxOwner& owner,
                                                            std::uint64_t attachEntityKey,
                                                            std::uint32_t instanceSeed);
    static void ApplyParameterOverrides(
        VfxInstance& instance,
        const std::map<std::string, ParameterOverride, std::less<>>& overrides) noexcept;
    void UpdateSummaryStats() noexcept;

    std::filesystem::path m_projectRoot;
    std::size_t m_globalBudget = kDefaultVfxGlobalBudget;
    std::uint8_t m_qualityLevel = 2U;
    std::uint32_t m_nextAutoSeed = 1U;
    std::unordered_map<std::string, CachedAssetEntry> m_assetCache;
    std::vector<InstanceSlot> m_slots;
    std::vector<std::uint32_t> m_freeSlotIndices;
    std::vector<VfxColliderVolume> m_sceneColliders;
    float m_hotReloadPollAccumulator = 0.0F;
    VfxWorldStats m_stats{};
};

} // namespace UVE::VFX
