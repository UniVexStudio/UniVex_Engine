// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_layout_store.h"

#include "application_runtime.h"
#include "prism/Containers.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace UVE::Editor {
namespace {

constexpr std::string_view kLayoutFileName = "editor_layout.json";
constexpr std::string_view kLayoutDirName = "UniVex";
constexpr int kLayoutSchemaVersion = 1;

} // namespace

EditorLayoutStore& EditorLayoutStore::Instance() {
    static EditorLayoutStore store;
    return store;
}

void EditorLayoutStore::SetStoragePathForTest(std::filesystem::path path) {
    m_overridePath = std::move(path);
    m_splitters.clear();
    m_bools.clear();
    m_ints.clear();
    m_windows.clear();
    m_loaded = false;
    EnsureLoaded();
}

std::filesystem::path EditorLayoutStore::ResolvePath() const {
    if (!m_overridePath.empty()) {
        return m_overridePath;
    }
    const Platform::ApplicationUserDataDirectoryResult dir = Platform::ResolveToolUserDataDirectory(kLayoutDirName);
    if (!dir.success || dir.directory.empty()) {
        return {};
    }
    return dir.directory / kLayoutFileName;
}

void EditorLayoutStore::EnsureLoaded() const {
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    LoadFromDisk();
}

void EditorLayoutStore::LoadFromDisk() const {
    const std::filesystem::path path = ResolvePath();
    if (path.empty()) {
        return;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return;
    }
    if (const auto sp = doc.find("splitters"); sp != doc.end() && sp->is_object()) {
        for (auto it = sp->begin(); it != sp->end(); ++it) {
            if (!it.value().is_array()) {
                continue;
            }
            std::vector<float> sizes;
            for (const auto& v : it.value()) {
                if (v.is_number()) {
                    const float f = v.get<float>();
                    if (std::isfinite(f) && f > 0.0F) {
                        sizes.push_back(f);
                    }
                }
            }
            if (!sizes.empty()) {
                m_splitters[it.key()] = std::move(sizes);
            }
        }
    }
    if (const auto bs = doc.find("bools"); bs != doc.end() && bs->is_object()) {
        for (auto it = bs->begin(); it != bs->end(); ++it) {
            if (it.value().is_boolean()) {
                m_bools[it.key()] = it.value().get<bool>();
            }
        }
    }
    if (const auto is = doc.find("ints"); is != doc.end() && is->is_object()) {
        for (auto it = is->begin(); it != is->end(); ++it) {
            if (it.value().is_number_integer()) {
                m_ints[it.key()] = it.value().get<int>();
            }
        }
    }
    if (const auto ws = doc.find("windows"); ws != doc.end() && ws->is_object()) {
        for (auto it = ws->begin(); it != ws->end(); ++it) {
            if (!it.value().is_object()) {
                continue;
            }
            const nlohmann::json& obj = it.value();
            SavedWindowBounds b;
            b.width = obj.value("w", 0);
            b.height = obj.value("h", 0);
            b.x = obj.value("x", 0);
            b.y = obj.value("y", 0);
            b.hasPosition = obj.value("hasPos", false);
            b.maximized = obj.value("maximized", false);
            b.pageFloating = obj.value("pageFloating", false);
            b.pageX = obj.value("pageX", 0.0F);
            b.pageY = obj.value("pageY", 0.0F);
            b.pageW = obj.value("pageW", 0.0F);
            b.pageH = obj.value("pageH", 0.0F);
            b.hasPageBounds = obj.value("hasPageBounds", false);
            m_windows[it.key()] = b;
        }
    }
}

void EditorLayoutStore::SaveToDisk() const {
    const std::filesystem::path path = ResolvePath();
    if (path.empty()) {
        return;
    }
    nlohmann::json doc;
    doc["schemaVersion"] = kLayoutSchemaVersion;
    nlohmann::json splitters = nlohmann::json::object();
    for (const auto& [k, v] : m_splitters) {
        splitters[k] = v;
    }
    doc["splitters"] = std::move(splitters);
    nlohmann::json bools = nlohmann::json::object();
    for (const auto& [k, v] : m_bools) {
        bools[k] = v;
    }
    doc["bools"] = std::move(bools);
    nlohmann::json ints = nlohmann::json::object();
    for (const auto& [k, v] : m_ints) {
        ints[k] = v;
    }
    doc["ints"] = std::move(ints);
    nlohmann::json windows = nlohmann::json::object();
    for (const auto& [k, b] : m_windows) {
        windows[k] = nlohmann::json{
            {"w", b.width},
            {"h", b.height},
            {"x", b.x},
            {"y", b.y},
            {"hasPos", b.hasPosition},
            {"maximized", b.maximized},
            {"pageFloating", b.pageFloating},
            {"pageX", b.pageX},
            {"pageY", b.pageY},
            {"pageW", b.pageW},
            {"pageH", b.pageH},
            {"hasPageBounds", b.hasPageBounds},
        };
    }
    doc["windows"] = std::move(windows);

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return;
        }
    }
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return;
        }
        output << doc.dump(2);
        output.flush();
        if (!output.good()) {
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            ec.clear();
            std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tmp, ec);
        }
    }
}

bool EditorLayoutStore::HasSplitter(const std::string_view key) const {
    EnsureLoaded();
    return m_splitters.find(std::string(key)) != m_splitters.end();
}

std::vector<float> EditorLayoutStore::GetSplitterSizes(const std::string_view key) const {
    EnsureLoaded();
    const auto it = m_splitters.find(std::string(key));
    return it != m_splitters.end() ? it->second : std::vector<float>{};
}

void EditorLayoutStore::SetSplitterSizes(const std::string_view key, const std::vector<float>& sizes) {
    EnsureLoaded();
    m_splitters[std::string(key)] = sizes;
    SaveToDisk();
}

void EditorLayoutStore::BindSplitter(std::string key, prism::Splitter& splitter) {
    EnsureLoaded();
    if (const auto it = m_splitters.find(key); it != m_splitters.end() && !it->second.empty()) {
        splitter.setPaneSizes(it->second);
    }
    std::weak_ptr<prism::Widget> weakSelf = splitter.weak_from_this();
    splitter.onChange = [this, key = std::move(key), weakSelf]() {
        if (const std::shared_ptr<prism::Widget> alive = weakSelf.lock()) {
            if (auto* const sp = dynamic_cast<prism::Splitter*>(alive.get())) {
                SetSplitterSizes(key, sp->paneSizes());
            }
        }
    };
}

bool EditorLayoutStore::GetBool(const std::string_view key, const bool fallback) const {
    EnsureLoaded();
    const auto it = m_bools.find(std::string(key));
    return it != m_bools.end() ? it->second : fallback;
}

void EditorLayoutStore::SetBool(const std::string_view key, const bool value) {
    EnsureLoaded();
    const std::string k(key);
    const auto it = m_bools.find(k);
    if (it != m_bools.end() && it->second == value) {
        return;
    }
    m_bools[k] = value;
    SaveToDisk();
}

int EditorLayoutStore::GetInt(const std::string_view key, const int fallback) const {
    EnsureLoaded();
    const auto it = m_ints.find(std::string(key));
    return it != m_ints.end() ? it->second : fallback;
}

void EditorLayoutStore::SetInt(const std::string_view key, const int value) {
    EnsureLoaded();
    const std::string k(key);
    const auto it = m_ints.find(k);
    if (it != m_ints.end() && it->second == value) {
        return;
    }
    m_ints[k] = value;
    SaveToDisk();
}

std::optional<SavedWindowBounds> EditorLayoutStore::GetWindowBounds(const std::string_view key) const {
    EnsureLoaded();
    const auto it = m_windows.find(std::string(key));
    if (it == m_windows.end()) {
        return std::nullopt;
    }
    return it->second;
}

void EditorLayoutStore::SetWindowBounds(const std::string_view key, const SavedWindowBounds& bounds) {
    EnsureLoaded();
    m_windows[std::string(key)] = bounds;
    SaveToDisk();
}

void EditorLayoutStore::ResetAll() {
    EnsureLoaded();
    m_splitters.clear();
    m_bools.clear();
    m_ints.clear();
    m_windows.clear();
    SaveToDisk();
}

} // namespace UVE::Editor
