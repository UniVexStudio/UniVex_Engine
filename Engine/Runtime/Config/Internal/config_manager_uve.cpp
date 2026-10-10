// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/config/config_manager_uve.h"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Config {

namespace {

/// Splits a dot-separated key path ("editor.theme") into its ordered
/// segments ({"editor", "theme"}). An empty `keyPath` yields a single
/// empty-string segment.
std::vector<std::string> SplitKeyPathUVE(std::string_view keyPath) {
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
const nlohmann::json* ResolveConstUVE(const nlohmann::json& document,
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
nlohmann::json& ResolveOrCreateUVE(nlohmann::json& document, const std::vector<std::string>& segments) {
    nlohmann::json* current = &document;
    for (const std::string& segment : segments) {
        if (!current->is_object()) {
            *current = nlohmann::json::object();
        }
        current = &(*current)[segment];
    }
    return *current;
}

/// Batch operations use the same dot path shape as the normal accessors, but refuse empty paths
/// and empty segments so a malformed transaction can never replace the root document.
[[nodiscard]] bool IsValidMutationPathUVE(const std::string_view path) noexcept {
    if (path.empty() || path.front() == '.' || path.back() == '.') {
        return false;
    }
    return path.find("..") == std::string_view::npos;
}

/// Removes one scalar leaf from `document` and prunes objects left empty by the removal.
bool RemoveKeyFromDocumentUVE(nlohmann::json& document, const std::vector<std::string>& segments) {
    if (segments.empty()) {
        return false;
    }
    std::vector<nlohmann::json*> parents{&document};
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
    for (std::size_t depth = parents.size() - 1U; depth > 0U && parents[depth]->empty(); --depth) {
        parents[depth - 1U]->erase(segments[depth - 1U]);
    }
    return true;
}

[[nodiscard]] std::optional<ConfigScalarValueUVE> ReadScalarValueUVE(const nlohmann::json* value) {
    if (value == nullptr || value->is_null() || value->is_object() || value->is_array()) {
        return std::nullopt;
    }
    if (value->is_boolean()) {
        return value->get<bool>();
    }
    if (value->is_string()) {
        return value->get<std::string>();
    }
    if (value->is_number_unsigned()) {
        const std::uint64_t number = value->get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            // The raw store has no uint64 alternative; retaining it as double preserves numeric
            // reads while typed Int descriptors will still reject it as outside int64 range.
            return static_cast<double>(number);
        }
        return static_cast<std::int64_t>(number);
    }
    if (value->is_number_integer()) {
        return value->get<std::int64_t>();
    }
    if (value->is_number_float()) {
        return value->get<double>();
    }
    return std::nullopt;
}

[[nodiscard]] nlohmann::json ToJsonValueUVE(const ConfigScalarValueUVE& value) {
    return std::visit([](const auto& scalar) { return nlohmann::json(scalar); }, value);
}

/// Pretty-prints `document` to `path`. Does not create missing parent
/// directories. On failure, logs a detailed Error containing both the
/// target path and the failure reason.
bool SaveDocumentToFileUVE(const nlohmann::json& document, const std::filesystem::path& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        UVE_ERROR("ConfigManagerUVE: failed to open \"{}\" for writing: {}", path.string(),
                   std::strerror(errno));
        return false;
    }

    file << document.dump(4);
    if (!file.good()) {
        UVE_ERROR("ConfigManagerUVE: failed to write settings to \"{}\": stream error after write",
                   path.string());
        return false;
    }
    return true;
}

} // namespace

/// The hidden implementation behind ConfigManagerUVE's PIMPL: the actual
/// nlohmann::json document, the path it was last loaded/saved with, and
/// the mutex guarding both. Defined only in this translation unit, so
/// nlohmann::json never appears in any header this module exposes.
struct ConfigManagerUVE::ImplUVE {
    mutable std::mutex mutex;
    nlohmann::json document = nlohmann::json::object();
    std::filesystem::path loadedPath;
};

ConfigManagerUVE::ConfigManagerUVE() : m_impl(std::make_unique<ImplUVE>()) {}

ConfigManagerUVE::~ConfigManagerUVE() = default;

bool ConfigManagerUVE::LoadUVE(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->loadedPath = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::error_code statusError;
        const bool exists = std::filesystem::exists(path, statusError);
        if (!exists && !statusError) {
            UVE_WARNING("ConfigManagerUVE: settings file not found at \"{}\" - starting with an "
                        "empty configuration",
                        path.string());
            m_impl->document = nlohmann::json::object();
            return false;
        }
        const std::string reason = statusError ? statusError.message() : std::strerror(errno);
        UVE_ERROR("ConfigManagerUVE: failed to open settings file \"{}\" for reading: {}", path.string(), reason);
        return false;
    }

    try {
        nlohmann::json parsed;
        file >> parsed;
        m_impl->document = std::move(parsed);
        return true;
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("ConfigManagerUVE: failed to parse settings file \"{}\": {}", path.string(),
                   parseError.what());
        return false;
    }
}

void ConfigManagerUVE::ClearAllUVE() {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->document = nlohmann::json::object();
}

void ConfigManagerUVE::ReplaceDocumentUVE(ConfigManagerUVE& candidate) {
    if (this == &candidate) {
        return;
    }
    const std::scoped_lock lock(m_impl->mutex, candidate.m_impl->mutex);
    m_impl->document.swap(candidate.m_impl->document);
    m_impl->loadedPath.swap(candidate.m_impl->loadedPath);
}

bool ConfigManagerUVE::SaveUVE() {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->loadedPath.empty()) {
        UVE_ERROR("ConfigManagerUVE: SaveUVE() called with no known target path - call LoadUVE() "
                   "or SaveUVE(path) first");
        return false;
    }
    return SaveDocumentToFileUVE(m_impl->document, m_impl->loadedPath);
}

bool ConfigManagerUVE::SaveUVE(const std::filesystem::path& path) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->loadedPath = path;
    return SaveDocumentToFileUVE(m_impl->document, path);
}

std::string ConfigManagerUVE::GetStringUVE(std::string_view keyPath, std::string_view defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConstUVE(m_impl->document, SplitKeyPathUVE(keyPath));
    if (value == nullptr || !value->is_string()) {
        return std::string(defaultValue);
    }
    return value->get<std::string>();
}

std::int64_t ConfigManagerUVE::GetIntUVE(std::string_view keyPath, std::int64_t defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConstUVE(m_impl->document, SplitKeyPathUVE(keyPath));
    if (value == nullptr || !value->is_number_integer()) {
        return defaultValue;
    }
    return value->get<std::int64_t>();
}

double ConfigManagerUVE::GetDoubleUVE(std::string_view keyPath, double defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConstUVE(m_impl->document, SplitKeyPathUVE(keyPath));
    if (value == nullptr || !value->is_number()) {
        return defaultValue;
    }
    return value->get<double>();
}

bool ConfigManagerUVE::GetBoolUVE(std::string_view keyPath, bool defaultValue) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConstUVE(m_impl->document, SplitKeyPathUVE(keyPath));
    if (value == nullptr || !value->is_boolean()) {
        return defaultValue;
    }
    return value->get<bool>();
}

void ConfigManagerUVE::SetStringUVE(std::string_view keyPath, std::string value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreateUVE(m_impl->document, SplitKeyPathUVE(keyPath)) = std::move(value);
}

void ConfigManagerUVE::SetIntUVE(std::string_view keyPath, std::int64_t value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreateUVE(m_impl->document, SplitKeyPathUVE(keyPath)) = value;
}

void ConfigManagerUVE::SetDoubleUVE(std::string_view keyPath, double value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreateUVE(m_impl->document, SplitKeyPathUVE(keyPath)) = value;
}

void ConfigManagerUVE::SetBoolUVE(std::string_view keyPath, bool value) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    ResolveOrCreateUVE(m_impl->document, SplitKeyPathUVE(keyPath)) = value;
}

std::vector<std::optional<ConfigScalarValueUVE>> ConfigManagerUVE::GetValuesUVE(
    const std::vector<std::string>& keyPaths) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::vector<std::optional<ConfigScalarValueUVE>> values;
    values.reserve(keyPaths.size());
    for (const std::string& keyPath : keyPaths) {
        const nlohmann::json* const value = ResolveConstUVE(m_impl->document, SplitKeyPathUVE(keyPath));
        values.push_back(ReadScalarValueUVE(value));
    }
    return values;
}

bool ConfigManagerUVE::ApplyMutationsUVE(const std::vector<ConfigMutationUVE>& mutations) {
    if (mutations.empty()) {
        return true;
    }

    try {
        std::vector<std::vector<std::string>> paths;
        paths.reserve(mutations.size());
        for (const ConfigMutationUVE& mutation : mutations) {
            if (!IsValidMutationPathUVE(mutation.keyPath)) {
                return false;
            }
            if (mutation.value) {
                if (const double* number = std::get_if<double>(&*mutation.value); number != nullptr &&
                    !std::isfinite(*number)) {
                    return false;
                }
            }
            paths.push_back(SplitKeyPathUVE(mutation.keyPath));
        }

        const std::lock_guard<std::mutex> lock(m_impl->mutex);
        nlohmann::json candidate = m_impl->document;
        for (std::size_t index = 0U; index < mutations.size(); ++index) {
            const ConfigMutationUVE& mutation = mutations[index];
            if (mutation.value) {
                ResolveOrCreateUVE(candidate, paths[index]) = ToJsonValueUVE(*mutation.value);
            } else {
                static_cast<void>(RemoveKeyFromDocumentUVE(candidate, paths[index]));
            }
        }
        m_impl->document.swap(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("ConfigManagerUVE: failed to apply an atomic settings update: {}", exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("ConfigManagerUVE: failed to apply an atomic settings update with an unknown exception");
        return false;
    }
}

bool ConfigManagerUVE::HasKeyUVE(std::string_view keyPath) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    const nlohmann::json* const value = ResolveConstUVE(m_impl->document, SplitKeyPathUVE(keyPath));
    return value != nullptr && !value->is_object();
}

bool ConfigManagerUVE::HasNodeUVE(std::string_view keyPath) const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    return ResolveConstUVE(m_impl->document, SplitKeyPathUVE(keyPath)) != nullptr;
}

bool ConfigManagerUVE::RemoveKeyUVE(std::string_view keyPath) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    return RemoveKeyFromDocumentUVE(m_impl->document, SplitKeyPathUVE(keyPath));
}

} // namespace UVE::Config
