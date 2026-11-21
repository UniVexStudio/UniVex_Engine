// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace UVE::Online {

/// Bounds that keep a damaged or hostile archive from filling the disk.
struct ArchiveLimitsUVE final {
    std::uint64_t maximumTotalBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
    std::size_t maximumEntries = 200000U;
};

struct ArchiveExtractResultUVE final {
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
[[nodiscard]] ArchiveExtractResultUVE ExtractTarGzUVE(const std::filesystem::path& archive,
                                                      const std::filesystem::path& destination,
                                                      const ArchiveLimitsUVE& limits = {});

} // namespace UVE::Online
