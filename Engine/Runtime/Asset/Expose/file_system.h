// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "i_asset_bundle.h"
#include "i_file_system.h"

namespace UVE::Asset {

/// FileSystem is the concrete, engine-standard implementation of IFileSystem. Bundle-backed
/// mounts read entries via the injected `IAssetBundle&` (dependency injection, same shape as
/// `AssetManager(IThreadPool&, IEventSystem&, ...)`) — no bundle-parsing logic is
/// duplicated here.
class FileSystem final : public IFileSystem {
public:
    explicit FileSystem(IAssetBundle& assetBundle);
    ~FileSystem() override;

    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    MountHandle MountDirectory(std::string virtualPrefix, std::filesystem::path realDirectory,
                                      int priority) override;
    MountHandle MountBundle(std::string virtualPrefix, std::filesystem::path bundlePath,
                                   int priority) override;
    void Unmount(MountHandle handle) override;

    [[nodiscard]] bool HasFile(std::string_view virtualPath) const override;
    [[nodiscard]] std::optional<std::vector<std::byte>> ReadFile(std::string_view virtualPath) const override;
    [[nodiscard]] bool WriteFile(std::string_view virtualPath, const std::vector<std::byte>& data) override;
    [[nodiscard]] std::filesystem::path ResolveRealPath(std::string_view virtualPath) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
