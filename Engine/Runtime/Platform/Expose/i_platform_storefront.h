// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UVE::Platform {

struct StorefrontAchievementUVE final {
    std::string id;
    bool unlocked = false;
    float progress01 = 0.0F;
    std::uint64_t unlockedTimestampUnix = 0U;
};

/// Generic, storefront-agnostic platform SDK abstraction covering achievements, player stats,
/// DLC/entitlements, and cloud-save slots.
class IPlatformStorefrontUVE {
public:
    virtual ~IPlatformStorefrontUVE() = default;
    [[nodiscard]] virtual std::string_view ProviderNameUVE() const noexcept = 0;

    virtual bool UnlockAchievementUVE(std::string_view achievementId) = 0;
    virtual bool SetAchievementProgressUVE(std::string_view achievementId, float progress01) = 0;
    [[nodiscard]] virtual bool IsAchievementUnlockedUVE(std::string_view achievementId) const = 0;

    virtual bool SetStatIntUVE(std::string_view statName, std::int64_t value) = 0;
    [[nodiscard]] virtual std::int64_t GetStatIntUVE(std::string_view statName,
                                                       std::int64_t defaultValue = 0) const = 0;

    [[nodiscard]] virtual bool HasEntitlementUVE(std::string_view entitlementId) const = 0;

    virtual bool WriteCloudFileUVE(std::string_view slotName,
                                   const std::vector<std::uint8_t>& bytes) = 0;
    [[nodiscard]] virtual bool ReadCloudFileUVE(std::string_view slotName,
                                                std::vector<std::uint8_t>& outBytes) const = 0;
};

/// Local in-memory / disk-backed mock storefront provider for development & automated testing.
class LocalMockPlatformStorefrontUVE final : public IPlatformStorefrontUVE {
public:
    explicit LocalMockPlatformStorefrontUVE(std::filesystem::path storageDir = {});

    [[nodiscard]] std::string_view ProviderNameUVE() const noexcept override { return "LocalMock"; }

    bool UnlockAchievementUVE(std::string_view achievementId) override;
    bool SetAchievementProgressUVE(std::string_view achievementId, float progress01) override;
    [[nodiscard]] bool IsAchievementUnlockedUVE(std::string_view achievementId) const override;

    bool SetStatIntUVE(std::string_view statName, std::int64_t value) override;
    [[nodiscard]] std::int64_t GetStatIntUVE(std::string_view statName,
                                               std::int64_t defaultValue = 0) const override;

    void GrantEntitlementUVE(std::string entitlementId);
    [[nodiscard]] bool HasEntitlementUVE(std::string_view entitlementId) const override;

    bool WriteCloudFileUVE(std::string_view slotName,
                           const std::vector<std::uint8_t>& bytes) override;
    [[nodiscard]] bool ReadCloudFileUVE(std::string_view slotName,
                                        std::vector<std::uint8_t>& outBytes) const override;

private:
    std::filesystem::path m_storageDir;
    std::map<std::string, StorefrontAchievementUVE> m_achievements;
    std::unordered_map<std::string, std::int64_t> m_stats;
    std::unordered_set<std::string> m_entitlements;
    std::unordered_map<std::string, std::vector<std::uint8_t>> m_cloudFiles;
};

} // namespace UVE::Platform
