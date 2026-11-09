// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace UVE::Asset {

/// Project files used to be named `.uve<kind>` ("UniVex Engine"); they are now `.uv<kind>`
/// (`.uvscene`, `.uvmodel`, `.uventity`...), and the editor project `.uveditor` became `.uvproject`.
/// These helpers move an existing project over the first time it is opened, so nothing is lost.

/// The current extension (with its dot) for a pre-rename one, compared without case - ".uvescene"
/// gives ".uvscene". Empty when `extension` is not a legacy one.
[[nodiscard]] std::string_view GetRenamedExtensionUVE(std::string_view extension) noexcept;

/// When `currentPath` does not exist but the same file under its legacy extension does, renames that
/// file to `currentPath`. Returns true when it renamed one.
bool MigrateLegacyFileUVE(const std::filesystem::path& currentPath);

struct LegacyMigrationReportUVE final {
    std::size_t renamed = 0U;
    /// Legacy files left alone because a file with the new name was already there.
    std::size_t skipped = 0U;
};

/// Renames every file with a legacy extension under `root`, recursively. A missing root is fine.
LegacyMigrationReportUVE MigrateLegacyContentUVE(const std::filesystem::path& root);

/// Replaces legacy extensions written inside a text file - the asset registry lists paths - and
/// saves it. Returns true when the file changed.
bool RewriteLegacyExtensionsInTextFileUVE(const std::filesystem::path& file);

} // namespace UVE::Asset
