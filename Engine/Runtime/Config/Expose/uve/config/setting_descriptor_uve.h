// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace UVE::Config {

/// What kind of value a setting holds. Each maps onto the scalar store (IConfigManagerUVE): a
/// colour is stored as one number per channel under `<id>.r`, `.g`, `.b` (and `.a`), a vector as
/// one per component under `<id>.x`, `.y`, `.z`.
enum class SettingTypeUVE {
    Bool,
    Int,
    Float,
    String,
    /// A 64-bit value that must be one of the descriptor's enumEntries.
    Enum,
    Color,
    /// Three numbers; the descriptor's bounds, if any, apply to each component.
    Vector3,
};

/// A colour setting's value: channels in 0..1. `a` is ignored for a colour without alpha.
struct SettingColorUVE final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    [[nodiscard]] bool operator==(const SettingColorUVE&) const = default;
};

/// A vector setting's value.
struct SettingVector3UVE final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] bool operator==(const SettingVector3UVE&) const = default;
};

/// One setting's value. The alternative in use always matches the descriptor's type: bool for
/// Bool, int64 for Int and Enum, double for Float, string for String, SettingColorUVE for Color,
/// SettingVector3UVE for Vector3.
using SettingValueUVE = std::variant<bool, std::int64_t, double, std::string, SettingColorUVE, SettingVector3UVE>;

/// Flags that describe a setting to the tools around it. The registry stores them; the settings
/// panel, layering and migration act on them.
enum SettingFlagUVE : std::uint32_t {
    kSettingFlagNoneUVE = 0U,
    /// A change takes effect only after a restart.
    kSettingFlagRestartRequiredUVE = 1U << 0U,
    /// Shown only when the settings panel is showing advanced settings.
    kSettingFlagAdvancedUVE = 1U << 1U,
    /// Never shown in the settings panel, but still stored and read.
    kSettingFlagHiddenUVE = 1U << 2U,
    /// May be overridden per target platform.
    kSettingFlagPerPlatformUVE = 1U << 3U,
    /// Lives for the session only; not written to the settings file.
    kSettingFlagNotPersistedUVE = 1U << 4U,
    /// Read to migrate an old value, never written.
    kSettingFlagDeprecatedUVE = 1U << 5U,
};

/// One named choice of an Enum setting.
struct SettingEnumEntryUVE final {
    std::int64_t value = 0;
    std::string label;
};

/// Everything there is to know about one setting: where it is stored, what it holds, what is
/// legal, what it defaults to, and how a person should see it. Declared next to the system that
/// owns the setting, and collected by SettingsRegistryUVE.
struct SettingDescriptorUVE final {
    /// Dot path into the settings document, e.g. "editor.viewport.grid.opacity". Also the id.
    std::string id;
    SettingTypeUVE type = SettingTypeUVE::Bool;
    /// The engine default, of the type's own alternative. What "reset to default" restores.
    SettingValueUVE defaultValue = false;
    /// Inclusive bounds for Int, Float and each component of a Vector3; absent means unbounded.
    std::optional<double> minimum;
    std::optional<double> maximum;
    /// Suggested increment for a slider or drag; purely a UI hint.
    std::optional<double> step;
    /// The legal values of an Enum setting, in display order.
    std::vector<SettingEnumEntryUVE> enumEntries;
    /// Longest legal String, in bytes; 0 means unbounded.
    std::size_t maxLength = 0U;
    /// Whether a Color setting carries alpha.
    bool colorHasAlpha = false;
    std::string displayName;
    std::string tooltip;
    /// Where it appears in the settings tree, as a slash path, e.g. "Editor/Viewport/Grid".
    std::string category;
    std::uint32_t flags = kSettingFlagNoneUVE;

    [[nodiscard]] bool HasFlagUVE(const SettingFlagUVE flag) const noexcept { return (flags & flag) != 0U; }
};

/// Whether `value` is legal for `descriptor`: the right alternative for its type and within every
/// constraint it declares. Not-a-number and infinity are never legal for a number or a channel.
[[nodiscard]] bool IsSettingValueValidUVE(const SettingDescriptorUVE& descriptor, const SettingValueUVE& value);

/// Empty when `descriptor` is well formed; otherwise one sentence saying what is wrong: an id
/// that is not a dot path, a default that breaks the descriptor's own constraints, bounds the
/// wrong way round, an Enum with no entries or with repeated values, and so on.
[[nodiscard]] std::string ValidateSettingDescriptorUVE(const SettingDescriptorUVE& descriptor);

// Declaration helpers, so a setting reads as one line at the place that owns it.
[[nodiscard]] SettingDescriptorUVE MakeBoolSettingUVE(std::string id, bool defaultValue, std::string displayName,
                                                      std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptorUVE MakeIntSettingUVE(std::string id, std::int64_t defaultValue,
                                                     std::optional<std::int64_t> minimum,
                                                     std::optional<std::int64_t> maximum, std::string displayName,
                                                     std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptorUVE MakeFloatSettingUVE(std::string id, double defaultValue,
                                                       std::optional<double> minimum, std::optional<double> maximum,
                                                       std::string displayName, std::string category,
                                                       std::string tooltip = {});
[[nodiscard]] SettingDescriptorUVE MakeStringSettingUVE(std::string id, std::string defaultValue,
                                                        std::size_t maxLength, std::string displayName,
                                                        std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptorUVE MakeEnumSettingUVE(std::string id, std::int64_t defaultValue,
                                                      std::vector<SettingEnumEntryUVE> entries,
                                                      std::string displayName, std::string category,
                                                      std::string tooltip = {});
[[nodiscard]] SettingDescriptorUVE MakeColorSettingUVE(std::string id, SettingColorUVE defaultValue, bool hasAlpha,
                                                       std::string displayName, std::string category,
                                                       std::string tooltip = {});
[[nodiscard]] SettingDescriptorUVE MakeVector3SettingUVE(std::string id, SettingVector3UVE defaultValue,
                                                         std::optional<double> minimum, std::optional<double> maximum,
                                                         std::string displayName, std::string category,
                                                         std::string tooltip = {});

} // namespace UVE::Config
