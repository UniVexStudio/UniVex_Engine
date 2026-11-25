// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <memory>

#include "i_project_file_index.h"

namespace UVE::Asset {

/// Engine-standard cached, read-only project-content index. The configured
/// root is normalized once at construction; each successful Refresh() builds
/// a complete replacement snapshot before publishing it to readers.
class ProjectFileIndex final : public IProjectFileIndex {
public:
    explicit ProjectFileIndex(std::filesystem::path contentRoot);
    ~ProjectFileIndex() override;

    ProjectFileIndex(const ProjectFileIndex&) = delete;
    ProjectFileIndex& operator=(const ProjectFileIndex&) = delete;
    ProjectFileIndex(ProjectFileIndex&&) = delete;
    ProjectFileIndex& operator=(ProjectFileIndex&&) = delete;

    bool Refresh(const IAssetDatabase& assetDatabase) override;
    [[nodiscard]] ProjectFileSnapshot GetSnapshot() const override;
    [[nodiscard]] std::uint64_t GetRefreshGeneration() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
