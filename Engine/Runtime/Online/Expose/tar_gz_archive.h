// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace UVE::Online {

/// File name extension of a gzip-compressed tar archive.
inline constexpr std::string_view kTarGzFileExtension = ".tar.gz";

/// Default ceiling on what one archive may write: far above any engine package, far below a full disk.
inline constexpr std::uint64_t kDefaultArchiveMaximumTotalBytes = std::uint64_t{8} << 30U;
/// Default ceiling on the number of entries one archive may hold.
inline constexpr std::size_t kDefaultArchiveMaximumEntries = 200000U;

/// Bounds that keep a damaged or hostile archive from filling the disk.
struct ArchiveLimits final {
    std::uint64_t maximumTotalBytes = kDefaultArchiveMaximumTotalBytes;
    std::size_t maximumEntries = kDefaultArchiveMaximumEntries;
    /// When set, only entries whose archive path it accepts are written; the rest are skipped. The
    /// safety checks still apply to every entry.
    std::function<bool(const std::filesystem::path&)> include;
};

struct ArchiveExtractResult final {
    bool extracted = false;
    std::string message;
    std::uint64_t bytesWritten = 0U;
    std::size_t entries = 0U;
};

/// Extracts a gzip-compressed tar archive (ustar, with GNU long names and pax paths) into
/// `destination`, which must exist and be empty. Only regular files and directories are written;
/// links, devices and any entry whose path is absolute or climbs out of `destination` fail the whole
/// extraction. Executable permission bits are kept where the platform has them. On failure the
/// destination may hold a partial tree; callers extract into a staging directory they can discard.
[[nodiscard]] ArchiveExtractResult ExtractTarGz(const std::filesystem::path& archive,
                                                      const std::filesystem::path& destination,
                                                      const ArchiveLimits& limits = {});

} // namespace UVE::Online
