// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asset_guid.h"

namespace UVE::Asset {

/// Bounds mirror the animation library's: a library never needs more entries than a player lists,
/// and identifiers stay short enough for stable hashing and logging.
inline constexpr std::size_t kMaximumMontageLibraryEntriesUVE = 256U;
inline constexpr std::size_t kMaximumMontageLibraryIdentifierBytesUVE = 128U;
inline constexpr std::size_t kMaximumMontageLibraryPayloadBytesUVE = 1U * 1024U * 1024U;

/// One montage in a library: the montage's GUID (how the player resolves it) plus its name, the
/// one a script plays it by (montage.play("Attack")).
struct MontageLibraryEntryUVE final {
    AssetGuidUVE montage{};
    std::string name;
    [[nodiscard]] bool operator==(const MontageLibraryEntryUVE&) const noexcept = default;
};

/// A montage library (`.uvmontagelib`): a named, ordered collection of `.uvmontage` Animation
/// Montages that MontagePlayers link to, beside their own slots - the montage counterpart of the
/// AnimationSequencer's `.uvanimlib`.
struct MontageLibraryAssetUVE final {
    std::string libraryId;
    std::vector<MontageLibraryEntryUVE> entries;
    [[nodiscard]] bool operator==(const MontageLibraryAssetUVE&) const noexcept = default;
};

/// Validates the CPU library descriptor before persistence or handoff. The id is non-empty and
/// bounded with no NUL; entries are bounded (empty is valid — a freshly created library);
/// every entry names a valid (non-zero) montage GUID exactly once with a non-empty, bounded,
/// NUL-free display name.
[[nodiscard]] bool IsMontageLibraryAssetValidUVE(const MontageLibraryAssetUVE& library) noexcept;

/// Loads `path` as a `.uve*` envelope with `AssetKindUVE::MontageLibrary`, filling `outLibrary`.
/// Returns false (logging the reason) if the file is missing/malformed, isn't actually an
/// MontageLibrary asset, or fails validation.
[[nodiscard]] bool LoadMontageLibraryAssetUVE(const std::filesystem::path& path,
                                                MontageLibraryAssetUVE& outLibrary);

/// Writes `library` to `path` as a `.uve*` envelope with `AssetKindUVE::MontageLibrary`.
/// Returns false (logging the reason) for an invalid descriptor or a file publication failure;
/// invalid descriptors are rejected before opening the destination.
[[nodiscard]] bool SaveMontageLibraryAssetUVE(const MontageLibraryAssetUVE& library,
                                                const std::filesystem::path& path);

} // namespace UVE::Asset
