// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "i_asset_database.h"

namespace UVE::Asset {

/// AssetDatabase is the concrete, engine-standard implementation of IAssetDatabase,
/// backed by nlohmann::json exactly like ConfigManager (engine/config/) — the JSON library
/// is entirely confined to asset_database_uve.cpp via the private Impl PIMPL, so this header
/// (and every consumer) never sees nlohmann::json. The on-disk format is a flat JSON object
/// mapping each GUID's hex string to its registered file path.
/// Thread-safety: thread-safe. Every method is guarded by an internal mutex — safe to call
/// concurrently from any thread.
class AssetDatabase final : public IAssetDatabase {
public:
    AssetDatabase();
    ~AssetDatabase() override;

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    /// Resolve a shipped relative catalog against its package root. Rejects escapes, missing
    /// files and symlinks leaving the root; failure preserves the previous registry.
    [[nodiscard]] bool RootPaths(const std::filesystem::path& root);
    /// Re-points registry entries whose file is gone because the project folder moved (another
    /// drive, a renamed folder, a copy on another machine). An entry is moved only when the part of
    /// its stored path after a folder named like `contentRoot` exists under `contentRoot` today -
    /// so GUIDs stay attached to their files and old entity references keep resolving. Returns the
    /// number of entries moved; the caller saves the registry when it is not zero. Relative entries
    /// are left alone (they belong to a package or project root, not to the process directory), and
    /// so is an entry whose target file already belongs to another GUID.
    std::size_t RelocateMovedContent(const std::filesystem::path& contentRoot);
    bool Load(const std::filesystem::path& path) override;
    bool Save() override;
    bool Save(const std::filesystem::path& path) override;

    [[nodiscard]] AssetGuid Register(const std::filesystem::path& assetPath) override;
    bool Relocate(AssetGuid guid, const std::filesystem::path& newPath) override;
    [[nodiscard]] std::filesystem::path Resolve(AssetGuid guid) const override;
    [[nodiscard]] bool HasGuid(AssetGuid guid) const override;
    [[nodiscard]] std::vector<AssetRecord> GetRegisteredAssets() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
