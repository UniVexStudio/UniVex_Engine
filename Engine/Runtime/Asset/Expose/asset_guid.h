// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <functional>

namespace UVE::Asset {

/// AssetGuid identifies an asset (a .uvprefab, and in the future any other .uve* asset)
/// independent of its file path — the mechanism the master spec requires scenes/prefabs to use
/// instead of embedding file-path dependencies. Deliberately an engine-custom 64-bit id, not a
/// full 128-bit UUID: a real GUID scheme (with generation/collision guarantees suitable for a
/// shared marketplace of assets) is Part 7.4's (AssetDatabase/AssetManager) job — this is
/// the smallest placeholder sufficient for IAssetDatabase to resolve prefab/scene
/// cross-references within a single project today.
/// Thread-safety: value type; safe to copy/compare/hash freely, no shared state.
struct AssetGuid {
    std::uint64_t value = 0;
};

/// The sentinel "no asset" value.
inline constexpr AssetGuid kInvalidAssetGuid{};

[[nodiscard]] constexpr bool operator==(const AssetGuid& lhs, const AssetGuid& rhs) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator!=(const AssetGuid& lhs, const AssetGuid& rhs) noexcept {
    return !(lhs == rhs);
}

/// Generates a fresh, statistically-unique AssetGuid (drawn from a std::mt19937_64 seeded
/// once from std::random_device — 64 bits of entropy, negligible collision probability for a
/// single project's asset count).
[[nodiscard]] AssetGuid GenerateAssetGuid();

} // namespace UVE::Asset

template <>
struct std::hash<UVE::Asset::AssetGuid> {
    [[nodiscard]] std::size_t operator()(const UVE::Asset::AssetGuid& guid) const noexcept {
        return std::hash<std::uint64_t>{}(guid.value);
    }
};
