// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "i_config_manager.h"

namespace UVE::Config {

class SettingsDocument;

/// ConfigManager is the concrete, engine-standard implementation of
/// IConfigManager, backed by nlohmann::json. The JSON library is
/// entirely confined to config_manager_uve.cpp via the private Impl
/// PIMPL — this header, and every consumer of ConfigManager, never sees
/// the nlohmann::json type (see docs/CODING_STANDARDS.md's dependency
/// confinement note).
/// Thread-safety: thread-safe. Every method is guarded by an internal
/// mutex (`mutable std::mutex m_mutex;` in Impl) — safe to call
/// concurrently from any thread.
class ConfigManager final : public IConfigManager {
public:
    ConfigManager();
    ~ConfigManager() override;

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    bool Load(const std::filesystem::path& path) override;
    /// Replaces the in-memory JSON document with an empty object without changing the remembered save path.
    /// Thread-safe.
    void ClearAll();
    bool Save() override;
    bool Save(const std::filesystem::path& path) override;

    [[nodiscard]] std::string GetString(std::string_view keyPath,
                                            std::string_view defaultValue) const override;
    [[nodiscard]] std::int64_t GetInt(std::string_view keyPath,
                                          std::int64_t defaultValue) const override;
    [[nodiscard]] double GetDouble(std::string_view keyPath, double defaultValue) const override;
    [[nodiscard]] bool GetBool(std::string_view keyPath, bool defaultValue) const override;

    void SetString(std::string_view keyPath, std::string value) override;
    void SetInt(std::string_view keyPath, std::int64_t value) override;
    void SetDouble(std::string_view keyPath, double value) override;
    void SetBool(std::string_view keyPath, bool value) override;

    [[nodiscard]] bool HasKey(std::string_view keyPath) const override;
    bool RemoveKey(std::string_view keyPath) override;

private:
    friend class SettingsDocument;
    /// Distinguishes a missing path from a root-level object at reserved metadata keys.
    [[nodiscard]] bool HasNode(std::string_view keyPath) const;
    /// Atomically replaces this store with a fully loaded and migrated candidate.
    void ReplaceDocument(ConfigManager& candidate);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Config
