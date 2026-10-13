// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "uve/asset/asset_guid_uve.h"

namespace UVE::Asset {

/// Bounds mirror the animation clip's caps — a library never needs more entries than a sequencer
/// lists, and identifiers stay short enough for stable hashing and logging.
inline constexpr std::size_t kMaximumAnimationLibraryEntriesUVE = 256U;
inline constexpr std::size_t kMaximumAnimationLibraryIdentifierBytesUVE = 128U;
inline constexpr std::size_t kMaximumAnimationLibraryPayloadBytesUVE = 1U * 1024U * 1024U;

/// One clip in a library: the clip's GUID (how the sequencer resolves it) plus the display name
/// the library window and the player's list show. Names ride with the entry rather than being
/// read from the clip file so a library can label the same clip differently per context.
struct AnimationLibraryEntryUVE final {
    AssetGuidUVE clip{};
    std::string name;
    [[nodiscard]] bool operator==(const AnimationLibraryEntryUVE&) const noexcept = default;
};

/// The CPU-side, engine-native representation of an animation library asset: a named, persisted,
/// ordered collection of `.uvanim` clips that AnimationSequencer players load from, instead of
/// every player carrying only its own hand-picked list. Entry order is the list order.
struct AnimationLibraryAssetUVE final {
    std::string libraryId;
    std::vector<AnimationLibraryEntryUVE> entries;
    [[nodiscard]] bool operator==(const AnimationLibraryAssetUVE&) const noexcept = default;
};

/// Validates the CPU library descriptor before persistence or handoff. The id is non-empty and
/// bounded with no NUL; entries are bounded (empty is valid — a freshly created library);
/// every entry names a valid (non-zero) clip GUID exactly once with a non-empty, bounded,
/// NUL-free display name.
[[nodiscard]] bool IsAnimationLibraryAssetValidUVE(const AnimationLibraryAssetUVE& library) noexcept;

/// Loads `path` as a `.uve*` envelope with `AssetKindUVE::AnimationLibrary`, filling `outLibrary`.
/// Returns false (logging the reason) if the file is missing/malformed, isn't actually an
/// AnimationLibrary asset, or fails validation.
[[nodiscard]] bool LoadAnimationLibraryAssetUVE(const std::filesystem::path& path,
                                                AnimationLibraryAssetUVE& outLibrary);

/// Writes `library` to `path` as a `.uve*` envelope with `AssetKindUVE::AnimationLibrary`.
/// Returns false (logging the reason) for an invalid descriptor or a file publication failure;
/// invalid descriptors are rejected before opening the destination.
[[nodiscard]] bool SaveAnimationLibraryAssetUVE(const AnimationLibraryAssetUVE& library,
                                                const std::filesystem::path& path);

} // namespace UVE::Asset
