// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_database.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "logging_macros.h"

namespace UVE::Asset {

namespace {

[[nodiscard]] std::string ToHexString(std::uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
}

[[nodiscard]] std::uint64_t FromHexString(const std::string& hex) {
    return std::strtoull(hex.c_str(), nullptr, 16);
}

/// Stable in-process lookup identity for asset registration. AssetDatabase does not own a
/// project-root dependency and must not resolve relative paths through the process current working
/// directory. Identity is therefore lexical only: it collapses spelling aliases such as `a/../b`
/// while keeping distinct relative and absolute representations explicit to the caller.
[[nodiscard]] std::filesystem::path NormalizeAssetPathIdentity(const std::filesystem::path& path) {
    return path.lexically_normal();
}

[[nodiscard]] std::string AssetPathIdentityKey(const std::filesystem::path& path) {
    return NormalizeAssetPathIdentity(path).generic_string();
}

/// Writes `document` to `path`, logging a detailed Error (path + reason) on failure — the same
/// contract ConfigManager's save path uses.
[[nodiscard]] bool SaveDocumentToFile(const nlohmann::json& document, const std::filesystem::path& path) {
    std::filesystem::path tempPath = path;
    tempPath += ".uvdb.tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            UVE_ERROR("AssetDatabase: failed to open \"{}\" for writing: {}", tempPath.string(),
                      std::strerror(errno));
            return false;
        }
        file << document.dump(4);
        file.flush();
        if (!file.good()) {
            UVE_ERROR("AssetDatabase: failed to write registry to \"{}\": stream error after write",
                      tempPath.string());
            std::error_code rmErr;
            std::filesystem::remove(tempPath, rmErr);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tempPath, path, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(tempPath, path, std::filesystem::copy_options::overwrite_existing, ec);
            std::error_code rmErr;
            std::filesystem::remove(tempPath, rmErr);
            if (ec) {
                UVE_ERROR("AssetDatabase: failed to publish registry to \"{}\": {}", path.string(), ec.message());
                return false;
            }
        }
    }
    return true;
}

} // namespace

struct AssetDatabase::Impl {
    mutable std::mutex mutex;
    std::unordered_map<AssetGuid, std::filesystem::path> guidToPath;
    std::unordered_map<std::string, AssetGuid> pathToGuid;
    std::filesystem::path loadedPath;
};

AssetDatabase::AssetDatabase() : m_impl(std::make_unique<Impl>()) {}

AssetDatabase::~AssetDatabase() = default;

bool AssetDatabase::Load(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);

    std::ifstream file(path);
    if (!file.is_open()) {
        UVE_WARNING("AssetDatabase: registry file not found at \"{}\" - starting with an "
                    "empty registry",
                    path.string());
        m_impl->loadedPath = path;
        m_impl->guidToPath.clear();
        m_impl->pathToGuid.clear();
        return false;
    }

    try {
        nlohmann::json parsed;
        file >> parsed;

        std::unordered_map<AssetGuid, std::filesystem::path> guidToPath;
        std::unordered_map<std::string, AssetGuid> pathToGuid;
        for (const auto& [guidHex, pathValue] : parsed.items()) {
            const AssetGuid guid{FromHexString(guidHex)};
            const std::filesystem::path storedPath = std::filesystem::path(pathValue.get<std::string>()).lexically_normal();
            guidToPath.emplace(guid, storedPath);

            // Legacy registries can contain lexically equivalent spellings under different GUIDs.
            // Preserve every persisted record, but ensure new equivalent-path lookups deterministically
            // retain the numerically smallest existing GUID without resolving through process CWD.
            const std::string pathKey = AssetPathIdentityKey(storedPath);
            const auto existingIt = pathToGuid.find(pathKey);
            if (existingIt == pathToGuid.end() || guid.value < existingIt->second.value) {
                pathToGuid.insert_or_assign(pathKey, guid);
            }
        }
        m_impl->guidToPath = std::move(guidToPath);
        m_impl->pathToGuid = std::move(pathToGuid);
        m_impl->loadedPath = path;
        return true;
    } catch (const nlohmann::json::exception& parseError) {
        UVE_ERROR("AssetDatabase: failed to parse registry file \"{}\": {}", path.string(),
                   parseError.what());
        return false;
    }
}

std::size_t AssetDatabase::RelocateMovedContent(const std::filesystem::path& contentRoot) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::error_code error;
    if (contentRoot.empty() || !std::filesystem::is_directory(contentRoot, error)) {
        return 0U;
    }
    const std::filesystem::path normalizedRoot = contentRoot.lexically_normal();
    std::filesystem::path rootName = normalizedRoot.filename();
    if (rootName.empty()) {
        rootName = normalizedRoot.parent_path().filename();
    }
    if (rootName.empty()) {
        return 0U;
    }
    std::size_t moved = 0U;
    for (auto& [guid, storedPath] : m_impl->guidToPath) {
        // A relative entry is relative to its own package or project root, not to wherever this
        // process was started, so it is never judged (or rewritten) here.
        if (storedPath.empty() || !storedPath.is_absolute()) {
            continue;
        }
        const bool present = std::filesystem::exists(storedPath, error);
        if (present || error) {
            error.clear(); // present, or unknowable (permission): not "moved"
            continue;
        }
        // The tail after the LAST folder named like the content root: "D:/Old/MyGame/Content/Props/a.uventity"
        // becomes "Props/a.uventity" under today's content root.
        std::vector<std::filesystem::path> parts(storedPath.begin(), storedPath.end());
        std::size_t anchorIndex = parts.size();
        for (std::size_t index = 0U; index < parts.size(); ++index) {
            if (parts[index] == rootName) {
                anchorIndex = index;
            }
        }
        if (anchorIndex + 1U >= parts.size()) {
            continue;
        }
        std::filesystem::path candidate = normalizedRoot;
        for (std::size_t index = anchorIndex + 1U; index < parts.size(); ++index) {
            candidate /= parts[index];
        }
        if (!std::filesystem::is_regular_file(candidate, error)) {
            continue;
        }
        const std::string newKey = AssetPathIdentityKey(candidate.lexically_normal());
        // Another GUID already owns the file at its current place: leave this one alone rather than
        // make two GUIDs name one file.
        if (const auto taken = m_impl->pathToGuid.find(newKey);
            taken != m_impl->pathToGuid.end() && taken->second != guid) {
            continue;
        }
        const std::string oldKey = AssetPathIdentityKey(storedPath);
        if (const auto it = m_impl->pathToGuid.find(oldKey); it != m_impl->pathToGuid.end() && it->second == guid) {
            m_impl->pathToGuid.erase(it);
        }
        storedPath = candidate.lexically_normal();
        m_impl->pathToGuid.insert_or_assign(newKey, guid);
        ++moved;
    }
    if (moved > 0U) {
        UVE_INFO("AssetDatabase: re-pointed {} registry entr{} at the project's current content folder \"{}\"",
                 moved, moved == 1U ? "y" : "ies", normalizedRoot.string());
    }
    return moved;
}

bool AssetDatabase::RootPaths(const std::filesystem::path& root) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::error_code error;
    const auto canonicalRoot = std::filesystem::canonical(root, error);
    if (error) return false;
    auto candidate = m_impl->guidToPath;
    std::unordered_map<std::string, AssetGuid> reverse;
    for (auto& [guid, path] : candidate) {
        if (guid.value == 0 || path.empty() || path.is_absolute()) return false;
        const auto resolved = std::filesystem::canonical(canonicalRoot / path, error);
        if (error || !std::filesystem::is_regular_file(resolved, error) || error) return false;
        const auto relative = resolved.lexically_relative(canonicalRoot);
        if (relative.empty() || *relative.begin() == "..") return false;
        path = resolved;
        const auto key = AssetPathIdentityKey(path);
        if (const auto it = reverse.find(key); it == reverse.end() || guid.value < it->second.value)
            reverse.insert_or_assign(key, guid);
    }
    m_impl->guidToPath = std::move(candidate);
    m_impl->pathToGuid = std::move(reverse);
    return true;
}

bool AssetDatabase::Save() {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loadedPath.empty()) {
        UVE_ERROR("AssetDatabase: Save() called with no known target path - call Load() "
                  "or Save(path) first");
        return false;
    }

    nlohmann::json document = nlohmann::json::object();
    for (const auto& [guid, path] : m_impl->guidToPath) {
        document[ToHexString(guid.value)] = path.string();
    }
    return SaveDocumentToFile(document, m_impl->loadedPath);
}

bool AssetDatabase::Save(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->loadedPath = path;

    nlohmann::json document = nlohmann::json::object();
    for (const auto& [guid, storedPath] : m_impl->guidToPath) {
        document[ToHexString(guid.value)] = storedPath.string();
    }
    return SaveDocumentToFile(document, path);
}

AssetGuid AssetDatabase::Register(const std::filesystem::path& assetPath) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const std::string pathKey = AssetPathIdentityKey(assetPath);

    const auto existingIt = m_impl->pathToGuid.find(pathKey);
    if (existingIt != m_impl->pathToGuid.end()) {
        return existingIt->second;
    }

    AssetGuid guid = GenerateAssetGuid();
    while (m_impl->guidToPath.find(guid) != m_impl->guidToPath.end()) {
        guid = GenerateAssetGuid(); // vanishingly unlikely, guarded anyway
    }

    m_impl->guidToPath.emplace(guid, assetPath.lexically_normal());
    m_impl->pathToGuid.emplace(pathKey, guid);
    return guid;
}

bool AssetDatabase::Relocate(const AssetGuid guid, const std::filesystem::path& newPath) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto record = m_impl->guidToPath.find(guid);
    if (record == m_impl->guidToPath.end()) {
        return false;
    }
    const std::string newKey = AssetPathIdentityKey(newPath);
    if (const auto taken = m_impl->pathToGuid.find(newKey); taken != m_impl->pathToGuid.end()) {
        return taken->second == guid;
    }
    const std::string oldKey = AssetPathIdentityKey(record->second);
    if (const auto old = m_impl->pathToGuid.find(oldKey); old != m_impl->pathToGuid.end() && old->second == guid) {
        m_impl->pathToGuid.erase(old);
    }
    record->second = newPath.lexically_normal();
    m_impl->pathToGuid.emplace(newKey, guid);
    return true;
}

std::filesystem::path AssetDatabase::Resolve(AssetGuid guid) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto it = m_impl->guidToPath.find(guid);
    if (it == m_impl->guidToPath.end()) {
        return {};
    }
    return it->second;
}

bool AssetDatabase::HasGuid(AssetGuid guid) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->guidToPath.find(guid) != m_impl->guidToPath.end();
}

std::vector<AssetRecord> AssetDatabase::GetRegisteredAssets() const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);

    std::vector<AssetRecord> records;
    records.reserve(m_impl->guidToPath.size());
    for (const auto& [guid, path] : m_impl->guidToPath) {
        records.push_back(AssetRecord{.guid = guid, .path = path.lexically_normal()});
    }

    std::sort(records.begin(), records.end(), [](const AssetRecord& lhs, const AssetRecord& rhs) {
        const std::string lhsPath = lhs.path.generic_string();
        const std::string rhsPath = rhs.path.generic_string();
        if (lhsPath != rhsPath) {
            return lhsPath < rhsPath;
        }
        return lhs.guid.value < rhs.guid.value;
    });

    return records;
}

} // namespace UVE::Asset
