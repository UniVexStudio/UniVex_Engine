// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

/// What one pass of `UpgradeSceneContent` did.
struct SceneContentUpgradeReport final {
    /// Scene, prefab and entity files read.
    std::size_t scanned = 0U;
    /// Files rewritten in the current format (each one backed up first).
    std::size_t upgraded = 0U;
    /// Files left alone on purpose: not readable as a scene document, or a symbolic link.
    std::size_t skipped = 0U;
    /// Files that needed rewriting but could not be (backup or write failed); listed in `failures`.
    /// Only these make the caller try the project again on the next start.
    std::size_t failed = 0U;
    std::vector<std::string> failures;
    /// Where the originals of the upgraded files were copied. Empty when nothing was upgraded.
    std::filesystem::path backupDirectory;
};

/// The component name an older document's `name` means today: retired names map to their
/// replacements, and the old "UVE" suffix is dropped. Names already current come back unchanged.
[[nodiscard]] std::string_view CanonicalSceneComponentName(std::string_view name) noexcept;

/// Brings every `.uvscene`, `.uvprefab` and `.uventity` under `contentRoot` to the current document
/// form, so assets authored with an older engine keep working after an engine update:
///   * component keys written under a retired name (or with the old "UVE" suffix) are renamed to
///     the current name;
///   * asset paths inside the document that still use a legacy `.uve*` extension are rewritten to
///     the `.uv*` name the content migration renamed those files to.
/// The document is otherwise left exactly as it was - unknown components are not dropped - so the
/// pass is safe on content this engine cannot fully read. A file is only rewritten when something
/// changed; the original is first copied under `backupRoot`, mirroring its path below the content
/// root, and the new version is written beside the file and renamed over it, so an interrupted run
/// never leaves a truncated document. Symbolic links are skipped. Nothing is ever deleted. Running it
/// again on upgraded content changes nothing.
[[nodiscard]] SceneContentUpgradeReport UpgradeSceneContent(const std::filesystem::path& contentRoot,
                                                            const std::filesystem::path& backupRoot);

} // namespace UVE::Scene
