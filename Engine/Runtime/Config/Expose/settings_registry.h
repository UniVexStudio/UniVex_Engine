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

#include "i_config_manager.h"
#include "setting_descriptor.h"

namespace UVE::Config {

/// The settings the engine and editor know about, each described once, and typed, validated
/// access to their values in a settings store.
///
/// The store (IConfigManager) stays the JSON document it has always been; this is the
/// description layer on top. Reading goes through a descriptor, so a value that is missing, of the
/// wrong type or out of range comes back as the declared default - per setting, never by throwing
/// away the whole document. Writing goes through it too, so an illegal value never enters the
/// document at all. Composite values (colours and vectors) are checked whole; StringLists are
/// bounded ordered strings stored under `<id>.count` and `<id>.<index>`.
///
/// Not thread-safe: register everything at startup, before the registry is shared. Reads and
/// writes go to the store, which is itself safe to call from any thread.
class SettingsRegistry final {
public:
    /// Adds `descriptor`. Refused - nothing registered, false returned - when ValidateSettingDescriptor
    /// finds it malformed, its id is already registered, or the document could not hold it beside
    /// the registered settings: "a.b" holding a value rules out "a.b.c". A colour, vector, or
    /// StringList reserves its component or numbered paths, while siblings such as "outline" and
    /// "outline.thickness" remain legal.
    [[nodiscard]] bool Register(SettingDescriptor descriptor);

    [[nodiscard]] const SettingDescriptor* Find(std::string_view id) const;
    /// Every registered descriptor, in the order it was registered.
    [[nodiscard]] std::vector<const SettingDescriptor*> GetAll() const;
    [[nodiscard]] std::size_t GetCount() const noexcept { return m_descriptors.size(); }

    /// The stored value of setting `id`, or its default when it is missing or illegal. No value
    /// for an id that is not registered.
    [[nodiscard]] std::optional<SettingValue> GetValue(const IConfigManager& store, std::string_view id) const;
    /// Stores `value` for setting `id`. Refused, leaving the store untouched, for an unknown id, a
    /// Deprecated setting, or a value IsSettingValueValid rejects.
    [[nodiscard]] bool SetValue(IConfigManager& store, std::string_view id, const SettingValue& value) const;
    /// Stores the default of setting `id`.
    [[nodiscard]] bool Reset(IConfigManager& store, std::string_view id) const;
    /// Removes setting `id` from the store altogether (every component or list item), so it reads
    /// as its default here, or as whatever a lower layer holds. False for an unknown id or when the
    /// store held nothing for it.
    bool ClearValue(IConfigManager& store, std::string_view id) const;
    /// The value the store holds for setting `id`, only when it holds a legal one: nothing for a
    /// missing, mistyped or out-of-range value, or an unknown id. This is what a layer
    /// contributes; GetValue is this with the default filled in.
    [[nodiscard]] std::optional<SettingValue> GetStoredValue(const IConfigManager& store,
                                                                   std::string_view id) const;
    /// Migrates stored Deprecated descriptors with a `replacementId` to their live setting. A legal
    /// value already stored at the new id wins; legacy ids are removed only after a successful copy
    /// or when superseded by a legal new value. Returns true if the store changed.
    bool MigrateDeprecatedValues(IConfigManager& store) const;
    /// Whether the value in use differs from the default.
    [[nodiscard]] bool IsModified(const IConfigManager& store, std::string_view id) const;

    // Typed forms of GetValue. `fallback` is answered only for an id that is not registered, or
    // is registered under another type - a programming error a test should catch. GetInt also
    // reads Enum settings; GetString reads FilePath/KeyBinding, and GetStringList reads StringList.
    [[nodiscard]] bool GetBool(const IConfigManager& store, std::string_view id, bool fallback = false) const;
    [[nodiscard]] std::int64_t GetInt(const IConfigManager& store, std::string_view id,
                                         std::int64_t fallback = 0) const;
    [[nodiscard]] double GetFloat(const IConfigManager& store, std::string_view id, double fallback = 0.0) const;
    [[nodiscard]] std::string GetString(const IConfigManager& store, std::string_view id,
                                           std::string fallback = {}) const;
    [[nodiscard]] SettingStringList GetStringList(const IConfigManager& store, std::string_view id,
                                                        SettingStringList fallback = {}) const;
    [[nodiscard]] SettingColor GetColor(const IConfigManager& store, std::string_view id,
                                              SettingColor fallback = {}) const;
    [[nodiscard]] std::uint32_t GetLayerMask(const IConfigManager& store, std::string_view id,
                                                std::uint32_t fallback = 0U) const;
    [[nodiscard]] SettingVector2 GetVector2(const IConfigManager& store, std::string_view id,
                                                  SettingVector2 fallback = {}) const;
    [[nodiscard]] SettingVector3 GetVector3(const IConfigManager& store, std::string_view id,
                                                  SettingVector3 fallback = {}) const;
    [[nodiscard]] SettingVector4 GetVector4(const IConfigManager& store, std::string_view id,
                                                  SettingVector4 fallback = {}) const;

private:
    std::vector<std::unique_ptr<SettingDescriptor>> m_descriptors;
    std::unordered_map<std::string, const SettingDescriptor*> m_byId;
    /// Document paths holding a value (an id, or a composite/list component), and every path that
    /// is an object because a value sits beneath it. No path may be in both.
    std::unordered_set<std::string> m_values;
    std::unordered_set<std::string> m_branches;
};

} // namespace UVE::Config
