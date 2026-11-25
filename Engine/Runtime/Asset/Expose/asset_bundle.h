// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_asset_bundle.h"

namespace UVE::Asset {

/// AssetBundle is the concrete, engine-standard implementation of IAssetBundle. Holds no
/// persistent members of its own (same "stateless, dependencies passed per-call" shape as
/// SceneSerializer) — the payload layout (entry count, then per-entry
/// guid/name/data-length/data) is entirely private to asset_bundle_uve.cpp.
class AssetBundle final : public IAssetBundle {
public:
    AssetBundle() = default;

    [[nodiscard]] bool Pack(const std::vector<AssetBundleEntry>& entries,
                                const std::filesystem::path& bundlePath) override;
    [[nodiscard]] bool Unpack(const std::filesystem::path& bundlePath,
                                  const std::filesystem::path& outputDirectory) override;
    [[nodiscard]] bool HasEntry(const std::filesystem::path& bundlePath,
                                    std::string_view entryName) const override;
    [[nodiscard]] std::optional<std::vector<std::byte>>
    ReadEntry(const std::filesystem::path& bundlePath, std::string_view entryName) const override;
};

} // namespace UVE::Asset
