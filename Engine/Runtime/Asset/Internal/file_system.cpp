// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "file_system.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

#include "logging_macros.h"

namespace UVE::Asset {

namespace {

enum class MountKind { Directory, Bundle };

struct MountRecord {
    MountHandle handle{};
    MountKind kind{};
    std::string prefix;
    std::filesystem::path realPathOrBundlePath;
    int priority = 0;
};

/// True iff `virtualPath` matches `prefix` on a whole-segment boundary: either exactly equal, or
/// `virtualPath` starts with `prefix + "/"`. An empty `prefix` (root mount) matches everything.
[[nodiscard]] bool MatchesPrefix(std::string_view virtualPath, std::string_view prefix) {
    if (virtualPath.contains('\\') || prefix.contains('\\')) {
        return false;
    }
    if (prefix.empty()) {
        return true;
    }
    if (virtualPath.size() < prefix.size() || virtualPath.substr(0, prefix.size()) != prefix) {
        return false;
    }
    return virtualPath.size() == prefix.size() || virtualPath[prefix.size()] == '/';
}

/// Returns `virtualPath` with `prefix` (and any single separating '/') removed from the front.
[[nodiscard]] std::string_view StripPrefix(std::string_view virtualPath, std::string_view prefix) {
    if (prefix.empty()) {
        return virtualPath;
    }
    std::string_view remainder = virtualPath.substr(prefix.size());
    if (!remainder.empty() && remainder.front() == '/') {
        remainder.remove_prefix(1);
    }
    return remainder;
}

[[nodiscard]] bool IsSafeVirtualRemainder(std::string_view remainder) {
    const std::filesystem::path path{std::string(remainder)};
    if (remainder.empty() || remainder.contains('\\') || path.is_absolute() ||
        path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    return std::none_of(path.begin(), path.end(), [](const std::filesystem::path& component) {
        return component == "." || component == "..";
    });
}

/// Every mount whose prefix matches `virtualPath` (optionally restricted to `kindFilter`),
/// copied out and sorted by descending priority (ties broken by most-recently-mounted first).
[[nodiscard]] std::vector<MountRecord> FindMatchingMounts(const std::vector<MountRecord>& mounts,
                                                                 std::string_view virtualPath,
                                                                 std::optional<MountKind> kindFilter = {}) {
    std::vector<MountRecord> matches;
    for (const MountRecord& record : mounts) {
        if (kindFilter.has_value() && record.kind != *kindFilter) {
            continue;
        }
        if (MatchesPrefix(virtualPath, record.prefix)) {
            matches.push_back(record);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const MountRecord& lhs, const MountRecord& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return lhs.handle > rhs.handle;
    });
    return matches;
}

/// Reads `path`'s full contents into a byte buffer. Returns `std::nullopt` if the file can't be
/// opened or a read error occurs partway through — never throws.
[[nodiscard]] std::optional<std::vector<std::byte>> ReadRealFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);

    if (size > static_cast<std::streamoff>(std::numeric_limits<std::streamsize>::max())) {
        return std::nullopt;
    }
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (!file) {
            return std::nullopt;
        }
    }
    return data;
}

} // namespace

struct FileSystem::Impl {
    IAssetBundle& assetBundle;
    mutable std::mutex mutex;
    std::vector<MountRecord> mounts;
    MountHandle nextHandle = 1;
};

FileSystem::FileSystem(IAssetBundle& assetBundle) : m_impl(std::make_unique<Impl>(assetBundle)) {}

FileSystem::~FileSystem() = default;

MountHandle FileSystem::MountDirectory(std::string virtualPrefix, std::filesystem::path realDirectory,
                                                 int priority) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const MountHandle handle = m_impl->nextHandle++;
    m_impl->mounts.push_back(MountRecord{handle, MountKind::Directory, std::move(virtualPrefix),
                                             std::move(realDirectory), priority});
    return handle;
}

MountHandle FileSystem::MountBundle(std::string virtualPrefix, std::filesystem::path bundlePath,
                                              int priority) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const MountHandle handle = m_impl->nextHandle++;
    m_impl->mounts.push_back(
        MountRecord{handle, MountKind::Bundle, std::move(virtualPrefix), std::move(bundlePath), priority});
    return handle;
}

void FileSystem::Unmount(MountHandle handle) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::erase_if(m_impl->mounts, [handle](const MountRecord& record) { return record.handle == handle; });
}

bool FileSystem::HasFile(std::string_view virtualPath) const {
    std::vector<MountRecord> matches;
    {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        matches = FindMatchingMounts(m_impl->mounts, virtualPath);
    }

    for (const MountRecord& record : matches) {
        const std::string_view remainder = StripPrefix(virtualPath, record.prefix);
        if (!IsSafeVirtualRemainder(remainder)) {
            continue;
        }
        if (record.kind == MountKind::Directory) {
            const std::filesystem::path realPath = record.realPathOrBundlePath / std::string(remainder);
            if (std::filesystem::exists(realPath)) {
                return true;
            }
        } else if (m_impl->assetBundle.HasEntry(record.realPathOrBundlePath, remainder)) {
            return true;
        }
    }
    return false;
}

std::optional<std::vector<std::byte>> FileSystem::ReadFile(std::string_view virtualPath) const {
    std::vector<MountRecord> matches;
    {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        matches = FindMatchingMounts(m_impl->mounts, virtualPath);
    }

    for (const MountRecord& record : matches) {
        const std::string_view remainder = StripPrefix(virtualPath, record.prefix);
        if (!IsSafeVirtualRemainder(remainder)) {
            continue;
        }
        if (record.kind == MountKind::Directory) {
            const std::filesystem::path realPath = record.realPathOrBundlePath / std::string(remainder);
            std::optional<std::vector<std::byte>> data = ReadRealFile(realPath);
            if (data.has_value()) {
                return data;
            }
        } else {
            std::optional<std::vector<std::byte>> data =
                m_impl->assetBundle.ReadEntry(record.realPathOrBundlePath, remainder);
            if (data.has_value()) {
                return data;
            }
        }
    }

    UVE_ERROR("FileSystem: no mount resolves virtual path \"{}\"", virtualPath);
    return std::nullopt;
}

bool FileSystem::WriteFile(std::string_view virtualPath, const std::vector<std::byte>& data) {
    std::vector<MountRecord> matches;
    {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        matches = FindMatchingMounts(m_impl->mounts, virtualPath, MountKind::Directory);
    }
    if (matches.empty()) {
        UVE_ERROR("FileSystem: no writable (directory) mount for virtual path \"{}\"", virtualPath);
        return false;
    }

    const MountRecord& target = matches.front();
    const std::string_view remainder = StripPrefix(virtualPath, target.prefix);
    if (!IsSafeVirtualRemainder(remainder)) {
        UVE_ERROR("FileSystem: virtual path \"{}\" escapes its mount", virtualPath);
        return false;
    }
    const std::filesystem::path realPath = target.realPathOrBundlePath / std::string(remainder);

    if (realPath.has_parent_path()) {
        std::error_code errorCode;
        std::filesystem::create_directories(realPath.parent_path(), errorCode);
        if (errorCode && !std::filesystem::exists(realPath.parent_path())) {
            UVE_ERROR("FileSystem: failed to create directory \"{}\": {}", realPath.parent_path().string(),
                       errorCode.message());
            return false;
        }
    }

    std::ofstream file(realPath, std::ios::binary);
    if (!file.is_open()) {
        UVE_ERROR("FileSystem: failed to open \"{}\" for writing", realPath.string());
        return false;
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file.good()) {
        UVE_ERROR("FileSystem: failed to write \"{}\"", realPath.string());
        return false;
    }
    return true;
}

std::filesystem::path FileSystem::ResolveRealPath(std::string_view virtualPath) const {
    std::vector<MountRecord> matches;
    {
        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        matches = FindMatchingMounts(m_impl->mounts, virtualPath, MountKind::Directory);
    }

    for (const MountRecord& record : matches) {
        const std::string_view remainder = StripPrefix(virtualPath, record.prefix);
        if (!IsSafeVirtualRemainder(remainder)) {
            continue;
        }
        const std::filesystem::path realPath = record.realPathOrBundlePath / std::string(remainder);
        if (std::filesystem::exists(realPath)) {
            return realPath;
        }
    }
    return {};
}

} // namespace UVE::Asset
