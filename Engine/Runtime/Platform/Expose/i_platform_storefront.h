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

struct StorefrontAchievement final {
    std::string id;
    bool unlocked = false;
    float progress01 = 0.0F;
    std::uint64_t unlockedTimestampUnix = 0U;
};

/// Generic, storefront-agnostic platform SDK abstraction covering achievements, player stats,
/// DLC/entitlements, and cloud-save slots.
class IPlatformStorefront {
public:
    virtual ~IPlatformStorefront() = default;
    [[nodiscard]] virtual std::string_view ProviderName() const noexcept = 0;

    virtual bool UnlockAchievement(std::string_view achievementId) = 0;
    virtual bool SetAchievementProgress(std::string_view achievementId, float progress01) = 0;
    [[nodiscard]] virtual bool IsAchievementUnlocked(std::string_view achievementId) const = 0;

    virtual bool SetStatInt(std::string_view statName, std::int64_t value) = 0;
    [[nodiscard]] virtual std::int64_t GetStatInt(std::string_view statName,
                                                       std::int64_t defaultValue = 0) const = 0;

    [[nodiscard]] virtual bool HasEntitlement(std::string_view entitlementId) const = 0;

    virtual bool WriteCloudFile(std::string_view slotName,
                                   const std::vector<std::uint8_t>& bytes) = 0;
    [[nodiscard]] virtual bool ReadCloudFile(std::string_view slotName,
                                                std::vector<std::uint8_t>& outBytes) const = 0;
};

/// Local in-memory / disk-backed mock storefront provider for development & automated testing.
class LocalMockPlatformStorefront final : public IPlatformStorefront {
public:
    explicit LocalMockPlatformStorefront(std::filesystem::path storageDir = {});

    [[nodiscard]] std::string_view ProviderName() const noexcept override { return "LocalMock"; }

    bool UnlockAchievement(std::string_view achievementId) override;
    bool SetAchievementProgress(std::string_view achievementId, float progress01) override;
    [[nodiscard]] bool IsAchievementUnlocked(std::string_view achievementId) const override;

    bool SetStatInt(std::string_view statName, std::int64_t value) override;
    [[nodiscard]] std::int64_t GetStatInt(std::string_view statName,
                                               std::int64_t defaultValue = 0) const override;

    void GrantEntitlement(std::string entitlementId);
    [[nodiscard]] bool HasEntitlement(std::string_view entitlementId) const override;

    bool WriteCloudFile(std::string_view slotName,
                           const std::vector<std::uint8_t>& bytes) override;
    [[nodiscard]] bool ReadCloudFile(std::string_view slotName,
                                        std::vector<std::uint8_t>& outBytes) const override;

private:
    std::filesystem::path m_storageDir;
    std::map<std::string, StorefrontAchievement> m_achievements;
    std::unordered_map<std::string, std::int64_t> m_stats;
    std::unordered_set<std::string> m_entitlements;
    std::unordered_map<std::string, std::vector<std::uint8_t>> m_cloudFiles;
};

} // namespace UVE::Platform
