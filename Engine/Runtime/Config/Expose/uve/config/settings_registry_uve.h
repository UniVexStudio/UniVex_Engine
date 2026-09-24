// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "uve/config/i_config_manager_uve.h"
#include "uve/config/setting_descriptor_uve.h"

namespace UVE::Config {

/// The settings the engine and editor know about, each described once, and typed, validated
/// access to their values in a settings store.
///
/// The store (IConfigManagerUVE) stays the JSON document it has always been; this is the
/// description layer on top. Reading goes through a descriptor, so a value that is missing, of the
/// wrong type or out of range comes back as the declared default - per setting, never by throwing
/// away the whole document. Writing goes through it too, so an illegal value never enters the
/// document at all. Composite values (colours) are checked whole: one bad channel refuses the
/// write, or falls back to the default on read.
///
/// Not thread-safe: register everything at startup, before the registry is shared. Reads and
/// writes go to the store, which is itself safe to call from any thread.
class SettingsRegistryUVE final {
public:
    /// Adds `descriptor`. Refused - nothing registered, false returned - when ValidateSettingDescriptorUVE
    /// finds it malformed, its id is already registered, or the document could not hold it beside
    /// the registered settings: "a.b" holding a value rules out "a.b.c", and a colour's channel keys
    /// (`.r`, `.g`, `.b`, `.a`) and a vector's (`.x`, `.y`, `.z`) are its own. All are programming errors the registry's own test is
    /// there to catch.
    [[nodiscard]] bool RegisterUVE(SettingDescriptorUVE descriptor);

    [[nodiscard]] const SettingDescriptorUVE* FindUVE(std::string_view id) const;
    /// Every registered descriptor, in the order it was registered.
    [[nodiscard]] std::vector<const SettingDescriptorUVE*> GetAllUVE() const;
    [[nodiscard]] std::size_t GetCountUVE() const noexcept { return m_descriptors.size(); }

    /// The stored value of setting `id`, or its default when it is missing or illegal. No value
    /// for an id that is not registered.
    [[nodiscard]] std::optional<SettingValueUVE> GetValueUVE(const IConfigManagerUVE& store, std::string_view id) const;
    /// Stores `value` for setting `id`. Refused, leaving the store untouched, for an unknown id, a
    /// Deprecated setting, or a value IsSettingValueValidUVE rejects.
    [[nodiscard]] bool SetValueUVE(IConfigManagerUVE& store, std::string_view id, const SettingValueUVE& value) const;
    /// Stores the default of setting `id`.
    [[nodiscard]] bool ResetUVE(IConfigManagerUVE& store, std::string_view id) const;
    /// Removes setting `id` from the store altogether (every channel of a colour), so it reads as
    /// its default here, or as whatever a lower layer holds. False for an unknown id or when the
    /// store held nothing for it.
    bool ClearValueUVE(IConfigManagerUVE& store, std::string_view id) const;
    /// The value the store holds for setting `id`, only when it holds a legal one: nothing for a
    /// missing, mistyped or out-of-range value, or an unknown id. This is what a layer
    /// contributes; GetValueUVE is this with the default filled in.
    [[nodiscard]] std::optional<SettingValueUVE> GetStoredValueUVE(const IConfigManagerUVE& store,
                                                                   std::string_view id) const;
    /// Whether the value in use differs from the default.
    [[nodiscard]] bool IsModifiedUVE(const IConfigManagerUVE& store, std::string_view id) const;

    // Typed forms of GetValueUVE. `fallback` is answered only for an id that is not registered, or
    // is registered under another type - a programming error a test should catch. GetIntUVE also
    // reads Enum settings.
    [[nodiscard]] bool GetBoolUVE(const IConfigManagerUVE& store, std::string_view id, bool fallback = false) const;
    [[nodiscard]] std::int64_t GetIntUVE(const IConfigManagerUVE& store, std::string_view id,
                                         std::int64_t fallback = 0) const;
    [[nodiscard]] double GetFloatUVE(const IConfigManagerUVE& store, std::string_view id, double fallback = 0.0) const;
    [[nodiscard]] std::string GetStringUVE(const IConfigManagerUVE& store, std::string_view id,
                                           std::string fallback = {}) const;
    [[nodiscard]] SettingColorUVE GetColorUVE(const IConfigManagerUVE& store, std::string_view id,
                                              SettingColorUVE fallback = {}) const;
    [[nodiscard]] SettingVector3UVE GetVector3UVE(const IConfigManagerUVE& store, std::string_view id,
                                                  SettingVector3UVE fallback = {}) const;

private:
    std::vector<std::unique_ptr<SettingDescriptorUVE>> m_descriptors;
    std::unordered_map<std::string, const SettingDescriptorUVE*> m_byId;
    /// Document paths holding a value (an id, or a colour's or vector's component keys), and every path that
    /// is an object because a value sits beneath it. No path may be in both.
    std::unordered_set<std::string> m_values;
    std::unordered_set<std::string> m_branches;
};

} // namespace UVE::Config
