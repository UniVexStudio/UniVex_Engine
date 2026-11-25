// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "i_platform_storefront.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <utility>

namespace UVE::Platform {

LocalMockPlatformStorefront::LocalMockPlatformStorefront(std::filesystem::path storageDir)
    : m_storageDir(std::move(storageDir)) {}

bool LocalMockPlatformStorefront::UnlockAchievement(const std::string_view achievementId) {
    if (achievementId.empty()) {
        return false;
    }
    auto& ach = m_achievements[std::string(achievementId)];
    ach.id = std::string(achievementId);
    ach.unlocked = true;
    ach.progress01 = 1.0F;
    ach.unlockedTimestampUnix = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    return true;
}

bool LocalMockPlatformStorefront::SetAchievementProgress(const std::string_view achievementId,
                                                               const float progress01) {
    if (achievementId.empty()) {
        return false;
    }
    auto& ach = m_achievements[std::string(achievementId)];
    ach.id = std::string(achievementId);
    ach.progress01 = std::clamp(progress01, 0.0F, 1.0F);
    if (ach.progress01 >= 1.0F) {
        return UnlockAchievement(achievementId);
    }
    return true;
}

bool LocalMockPlatformStorefront::IsAchievementUnlocked(
    const std::string_view achievementId) const {
    const auto it = m_achievements.find(std::string(achievementId));
    return it != m_achievements.end() && it->second.unlocked;
}

bool LocalMockPlatformStorefront::SetStatInt(const std::string_view statName,
                                                   const std::int64_t value) {
    if (statName.empty()) {
        return false;
    }
    m_stats[std::string(statName)] = value;
    return true;
}

std::int64_t LocalMockPlatformStorefront::GetStatInt(const std::string_view statName,
                                                           const std::int64_t defaultValue) const {
    const auto it = m_stats.find(std::string(statName));
    return it != m_stats.end() ? it->second : defaultValue;
}

void LocalMockPlatformStorefront::GrantEntitlement(std::string entitlementId) {
    if (!entitlementId.empty()) {
        m_entitlements.insert(std::move(entitlementId));
    }
}

bool LocalMockPlatformStorefront::HasEntitlement(const std::string_view entitlementId) const {
    return m_entitlements.find(std::string(entitlementId)) != m_entitlements.end();
}

bool LocalMockPlatformStorefront::WriteCloudFile(const std::string_view slotName,
                                                       const std::vector<std::uint8_t>& bytes) {
    if (slotName.empty()) {
        return false;
    }
    m_cloudFiles[std::string(slotName)] = bytes;
    if (!m_storageDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(m_storageDir, ec);
        std::ofstream out(m_storageDir / std::string(slotName), std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
    }
    return true;
}

bool LocalMockPlatformStorefront::ReadCloudFile(const std::string_view slotName,
                                                      std::vector<std::uint8_t>& outBytes) const {
    const auto it = m_cloudFiles.find(std::string(slotName));
    if (it != m_cloudFiles.end()) {
        outBytes = it->second;
        return true;
    }
    if (!m_storageDir.empty()) {
        std::ifstream in(m_storageDir / std::string(slotName), std::ios::binary | std::ios::ate);
        if (in) {
            const std::streamsize sz = in.tellg();
            if (sz >= 0) {
                in.seekg(0, std::ios::beg);
                outBytes.resize(static_cast<std::size_t>(sz));
                if (sz == 0 || in.read(reinterpret_cast<char*>(outBytes.data()), sz)) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace UVE::Platform
