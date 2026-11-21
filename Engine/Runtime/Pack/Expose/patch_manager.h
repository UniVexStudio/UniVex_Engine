// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Pack {

struct MountedPatchEntryUVE final {
    std::string patchId;
    std::string version;
    std::int32_t priority = 0; // Higher priority overrides lower priority and base content.
    std::filesystem::path rootDirectory;
    std::string expectedManifestHashHex;
};

/// Computes a deterministic 64-bit FNV-1a lowercase hex digest of a file on disk.
[[nodiscard]] bool ComputeFileIntegrityHashHexUVE(const std::filesystem::path& filePath,
                                                  std::string& outHexDigest);

/// Manages priority-ordered downloadable content (DLC) and live hotfix patch mounts over a
/// base content directory, with optional integrity verification and clean rollback.
class PatchManagerUVE final {
public:
    explicit PatchManagerUVE(std::filesystem::path baseContentRoot = {});

    void SetBaseContentRootUVE(std::filesystem::path baseContentRoot);
    [[nodiscard]] const std::filesystem::path& BaseContentRootUVE() const noexcept {
        return m_baseContentRoot;
    }

    /// Mounts a patch directory (optionally verifying `manifestFilePath` against
    /// `entry.expectedManifestHashHex` when non-empty). Rejects duplicate patch IDs or
    /// mismatched integrity digests.
    [[nodiscard]] bool MountPatchUVE(MountedPatchEntryUVE entry,
                                     const std::filesystem::path& manifestFilePath = {});

    /// Cleanly rolls back and unmounts `patchId`. Returns true if it was mounted.
    bool RollbackPatchUVE(std::string_view patchId);

    /// Resolves `relativeAssetPath` against mounted patches in descending priority order,
    /// falling back to `baseContentRoot`. Returns empty path if not found anywhere.
    [[nodiscard]] std::filesystem::path ResolvePatchedAssetPathUVE(
        const std::filesystem::path& relativeAssetPath) const;

    [[nodiscard]] const std::vector<MountedPatchEntryUVE>& MountedPatchesUVE() const noexcept {
        return m_patches;
    }

private:
    std::filesystem::path m_baseContentRoot;
    std::vector<MountedPatchEntryUVE> m_patches;
};

} // namespace UVE::Pack
