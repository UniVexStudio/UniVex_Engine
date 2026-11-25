// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "patch_manager.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <utility>

namespace UVE::Pack {

bool ComputeFileIntegrityHashHex(const std::filesystem::path& filePath,
                                    std::string& outHexDigest) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) {
        return false;
    }
    std::uint64_t hash = 14695981039346656037ULL;
    char buf[4096];
    while (in) {
        in.read(buf, sizeof(buf));
        const std::streamsize count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<std::uint8_t>(buf[i]);
            hash *= 1099511628211ULL;
        }
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
    outHexDigest = hex;
    return true;
}

PatchManager::PatchManager(std::filesystem::path baseContentRoot)
    : m_baseContentRoot(std::move(baseContentRoot)) {}

void PatchManager::SetBaseContentRoot(std::filesystem::path baseContentRoot) {
    m_baseContentRoot = std::move(baseContentRoot);
}

bool PatchManager::MountPatch(MountedPatchEntry entry,
                                    const std::filesystem::path& manifestFilePath) {
    if (entry.patchId.empty() || entry.rootDirectory.empty()) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(entry.rootDirectory, ec)) {
        return false;
    }
    for (const auto& existing : m_patches) {
        if (existing.patchId == entry.patchId) {
            return false;
        }
    }
    if (!entry.expectedManifestHashHex.empty()) {
        const std::filesystem::path checkFile =
            manifestFilePath.empty() ? (entry.rootDirectory / "patch.manifest") : manifestFilePath;
        std::string actualHex;
        if (!ComputeFileIntegrityHashHex(checkFile, actualHex) ||
            actualHex != entry.expectedManifestHashHex) {
            return false;
        }
    }

    m_patches.push_back(std::move(entry));
    std::stable_sort(m_patches.begin(), m_patches.end(),
                     [](const MountedPatchEntry& a, const MountedPatchEntry& b) {
                         return a.priority > b.priority;
                     });
    return true;
}

bool PatchManager::RollbackPatch(const std::string_view patchId) {
    const auto it = std::find_if(m_patches.begin(), m_patches.end(),
                                 [patchId](const MountedPatchEntry& p) { return p.patchId == patchId; });
    if (it == m_patches.end()) {
        return false;
    }
    m_patches.erase(it);
    return true;
}

std::filesystem::path PatchManager::ResolvePatchedAssetPath(
    const std::filesystem::path& relativeAssetPath) const {
    if (relativeAssetPath.empty() || relativeAssetPath.is_absolute()) {
        return {};
    }
    std::error_code ec;
    for (const MountedPatchEntry& patch : m_patches) {
        const std::filesystem::path candidate = patch.rootDirectory / relativeAssetPath;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    if (!m_baseContentRoot.empty()) {
        const std::filesystem::path baseCandidate = m_baseContentRoot / relativeAssetPath;
        if (std::filesystem::exists(baseCandidate, ec)) {
            return baseCandidate;
        }
    }
    return {};
}

} // namespace UVE::Pack
