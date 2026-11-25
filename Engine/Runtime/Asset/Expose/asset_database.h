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
