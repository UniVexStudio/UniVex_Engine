// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "config_manager.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "logging_macros.h"

namespace UVE::Config {

namespace {

/// Splits a dot-separated key path ("editor.theme") into its ordered
/// segments ({"editor", "theme"}). An empty `keyPath` yields a single
/// empty-string segment.
std::vector<std::string> SplitKeyPath(std::string_view keyPath) {
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = keyPath.find('.', start);
        if (dot == std::string_view::npos) {
            segments.emplace_back(keyPath.substr(start));
            break;
        }
        segments.emplace_back(keyPath.substr(start, dot - start));
        start = dot + 1;
    }
    return segments;
}

/// Walks `segments` through `document`, returning a pointer to the object
/// reached, or nullptr if any segment along the way is missing or the
/// current object is not an object to walk into.
const nlohmann::json* ResolveConst(const nlohmann::json& document,
                                       const std::vector<std::string>& segments) {
    const nlohmann::json* current = &document;
    for (const std::string& segment : segments) {
        if (!current->is_object()) {
            return nullptr;
        }
        const auto memberIt = current->find(segment);
        if (memberIt == current->end()) {
            return nullptr;
        }
        current = &(*memberIt);
    }
    return current;
}

/// Walks `segments` through `document`, creating intermediate objects
/// along the way as needed (overwriting any non-object object found in an
/// intermediate position), and returns a reference to the final object.
nlohmann::json& ResolveOrCreate(nlohmann::json& document, const std::vector<std::string>& segments) {
    nlohmann::json* current = &document;
    for (const std::string& segment : segments) {
        if (!current->is_object()) {
            *current = nlohmann::json::object();
        }
        current = &(*current)[segment];
    }
    return *current;
}

/// Pretty-prints `document` to `path`. Does not create missing parent
/// directories. On failure, logs a detailed Error containing both the
/// target path and the failure reason.
bool SaveDocumentToFile(const nlohmann::json& document, const std::filesystem::path& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        UVE_ERROR("ConfigManager: failed to open \"{}\" for writing: {}", path.string(),
                   std::strerror(errno));
        return false;
    }

    file << document.dump(4);
    if (!file.good()) {
        UVE_ERROR("ConfigManager: failed to write settings to \"{}\": stream error after write",
                   path.string());
        return false;
    }
    return true;
}

} // namespace

/// The hidden implementation behind ConfigManager's PIMPL: the actual
/// nlohmann::json document, the path it was last loaded/saved with, and
/// the mutex guarding both. Defined only in this translation unit, so
/// nlohmann::json never appears in any header this module exposes.
struct ConfigManager::Impl {
    mutable std::mutex mutex;
    nlohmann::json document = nlohmann::json::object();
    std::filesystem::path loadedPath;
};

ConfigManager::ConfigManager() : m_impl(std::make_unique<Impl>()) {}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::Load(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->loadedPath = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::error_code statusError;
        const bool exists = std::filesystem::exists(path, statusError);
        if (!exists && !statusError) {
            UVE_WARNING("ConfigManager: settings file not found at \"{}\" - starting with an "
                        "empty configuration",
                        path.string());
            m_impl->document = nlohmann::json::object();
            return false;
        }
        const std::string reason = statusError ? statusError.message() : std::strerror(errno);
        UVE_ERROR("ConfigManager: failed to open settings file \"{}\" for reading: {}", path.string(), reason);
        return false;
    }

    try {
        nlohmann::json parsed;
        file >> parsed;
        m_impl->document = std::move(parsed);
        return true;
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("ConfigManager: failed to parse settings file \"{}\": {}", path.string(),
                   parseError.what());
        return false;
    }
}

void ConfigManager::ClearAll() {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->document = nlohmann::json::object();
}

void ConfigManager::ReplaceDocument(ConfigManager& candidate) {
    if (this == &candidate) {
        return;
    }
    const std::scoped_lock lock(m_impl->mutex, candidate.m_impl->mutex);
    m_impl->document.swap(candidate.m_impl->document);
    m_impl->loadedPath.swap(candidate.m_impl->loadedPath);
}

bool ConfigManager::Save() {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loadedPath.empty()) {
        UVE_ERROR("ConfigManager: Save() called with no known target path - call Load() "
                   "or Save(path) first");
        return false;
    }
    return SaveDocumentToFile(m_impl->document, m_impl->loadedPath);
}

bool ConfigManager::Save(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->loadedPath = path;
    return SaveDocumentToFile(m_impl->document, path);
}

std::string ConfigManager::GetString(std::string_view keyPath, std::string_view defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConst(m_impl->document, SplitKeyPath(keyPath));
    if (value == nullptr || !value->is_string()) {
        return std::string(defaultValue);
    }
    return value->get<std::string>();
}

std::int64_t ConfigManager::GetInt(std::string_view keyPath, std::int64_t defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConst(m_impl->document, SplitKeyPath(keyPath));
    if (value == nullptr || !value->is_number_integer()) {
        return defaultValue;
    }
    return value->get<std::int64_t>();
}

double ConfigManager::GetDouble(std::string_view keyPath, double defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConst(m_impl->document, SplitKeyPath(keyPath));
    if (value == nullptr || !value->is_number()) {
        return defaultValue;
    }
    return value->get<double>();
}

bool ConfigManager::GetBool(std::string_view keyPath, bool defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConst(m_impl->document, SplitKeyPath(keyPath));
    if (value == nullptr || !value->is_boolean()) {
        return defaultValue;
    }
    return value->get<bool>();
}

void ConfigManager::SetString(std::string_view keyPath, std::string value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreate(m_impl->document, SplitKeyPath(keyPath)) = std::move(value);
}

void ConfigManager::SetInt(std::string_view keyPath, std::int64_t value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreate(m_impl->document, SplitKeyPath(keyPath)) = value;
}

void ConfigManager::SetDouble(std::string_view keyPath, double value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreate(m_impl->document, SplitKeyPath(keyPath)) = value;
}

void ConfigManager::SetBool(std::string_view keyPath, bool value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreate(m_impl->document, SplitKeyPath(keyPath)) = value;
}

bool ConfigManager::HasKey(std::string_view keyPath) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConst(m_impl->document, SplitKeyPath(keyPath));
    return value != nullptr && !value->is_object();
}

bool ConfigManager::HasNode(std::string_view keyPath) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    return ResolveConst(m_impl->document, SplitKeyPath(keyPath)) != nullptr;
}

bool ConfigManager::RemoveKey(std::string_view keyPath) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const std::vector<std::string> segments = SplitKeyPath(keyPath);
    if (segments.empty()) {
        return false;
    }
    // The chain of objects from the root down to the leaf's parent.
    std::vector<nlohmann::json*> parents{&m_impl->document};
    for (std::size_t index = 0U; index + 1U < segments.size(); ++index) {
        nlohmann::json& parent = *parents.back();
        if (!parent.is_object()) {
            return false;
        }
        const auto member = parent.find(segments[index]);
        if (member == parent.end()) {
            return false;
        }
        parents.push_back(&*member);
    }
    nlohmann::json& parent = *parents.back();
    if (!parent.is_object()) {
        return false;
    }
    const auto leaf = parent.find(segments.back());
    if (leaf == parent.end() || leaf->is_object()) {
        return false;
    }
    parent.erase(leaf);
    // Prune objects the removal emptied, deepest first; the root always stays.
    for (std::size_t depth = parents.size() - 1U; depth > 0U && parents[depth]->empty(); --depth) {
        parents[depth - 1U]->erase(segments[depth - 1U]);
    }
    return true;
}

} // namespace UVE::Config
