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

/// Root-level metadata key reserved for the settings document schema version.
inline constexpr std::string_view kSettingsDocumentVersionKey = "version";

/// What kind of value a setting holds. Each maps onto the scalar store (IConfigManager): a
/// colour is stored as one number per channel under `<id>.r`, `.g`, `.b` (and `.a`), a vector as
/// one per component under `<id>.x`, `.y`, `.z`, and a StringList as `<id>.count` plus numbered
/// string values. FilePath and KeyBinding are stored as strings; KeyBinding uses stable names
/// rather than backend key codes.
enum class SettingType {
    Bool,
    Int,
    Float,
    String,
    /// A filesystem path stored as text, with no requirement that it exist yet.
    FilePath,
    /// A content-relative asset path or GUID string referencing an engine asset (empty = none).
    AssetReference,
    /// A stable keyboard chord such as "Ctrl+Shift+S"; an empty string means unbound.
    KeyBinding,
    /// An ordered list of strings, stored as a count and numbered values beneath the setting id.
    StringList,
    /// A 64-bit value that must be one of the descriptor's enumEntries.
    Enum,
    /// A 32-bit unsigned bitmask (stored as int64 in 0..0xFFFFFFFF) for physics/render/nav layers.
    LayerMask,
    Color,
    /// Two numbers; the descriptor's bounds, if any, apply to each component.
    Vector2,
    /// Three numbers; the descriptor's bounds, if any, apply to each component.
    Vector3,
    /// Four numbers; the descriptor's bounds, if any, apply to each component.
    Vector4,
};

/// A colour setting's value: channels in 0..1. `a` is ignored for a colour without alpha.
struct SettingColor final {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    [[nodiscard]] bool operator==(const SettingColor&) const = default;
};

/// A 2D vector setting's value.
struct SettingVector2 final {
    double x = 0.0;
    double y = 0.0;

    [[nodiscard]] bool operator==(const SettingVector2&) const = default;
};

/// A 3D vector setting's value.
struct SettingVector3 final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    [[nodiscard]] bool operator==(const SettingVector3&) const = default;
};

/// A 4D vector setting's value.
struct SettingVector4 final {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 0.0;

    [[nodiscard]] bool operator==(const SettingVector4&) const = default;
};

using SettingStringList = std::vector<std::string>;
inline constexpr std::size_t kMaximumSettingStringListItems = 4096U;

/// One setting's value. The alternative in use always matches the descriptor's type: bool for
/// Bool, int64 for Int/Enum/LayerMask, double for Float, string for String/FilePath/AssetReference/KeyBinding,
/// SettingStringList for StringList, SettingColor for Color, SettingVector2/3UVE/4UVE for vectors.
using SettingValue =
    std::variant<bool, std::int64_t, double, std::string, SettingStringList, SettingColor,
                 SettingVector2, SettingVector3, SettingVector4>;

/// Flags that describe a setting to the tools around it. The registry stores them; the settings
/// panel, layering and migration act on them.
enum SettingFlag : std::uint32_t {
    kSettingFlagNone = 0U,
    /// A change takes effect only after a restart.
    kSettingFlagRestartRequired = 1U << 0U,
    /// Shown only when the settings panel is showing advanced settings.
    kSettingFlagAdvanced = 1U << 1U,
    /// Never shown in the settings panel, but still stored and read.
    kSettingFlagHidden = 1U << 2U,
    /// May be overridden per target platform.
    kSettingFlagPerPlatform = 1U << 3U,
    /// Lives for the session only; not written to the settings file.
    kSettingFlagNotPersisted = 1U << 4U,
    /// Read to migrate an old value, never written.
    kSettingFlagDeprecated = 1U << 5U,
};

/// One named choice of an Enum setting.
struct SettingEnumEntry final {
    std::int64_t value = 0;
    std::string label;
};

/// Everything there is to know about one setting: where it is stored, what it holds, what is
/// legal, what it defaults to, and how a person should see it. Declared next to the system that
/// owns the setting, and collected by SettingsRegistry.
struct SettingDescriptor final {
    /// Dot path into the settings document, e.g. "editor.viewport.grid.opacity". Also the id.
    std::string id;
    SettingType type = SettingType::Bool;
    /// The engine default, of the type's own alternative. What "reset to default" restores.
    SettingValue defaultValue = false;
    /// Inclusive bounds for Int, Float and each component of a Vector3; absent means unbounded.
    std::optional<double> minimum;
    std::optional<double> maximum;
    /// Suggested increment for a slider or drag; purely a UI hint.
    std::optional<double> step;
    /// The legal values of an Enum setting, in display order.
    std::vector<SettingEnumEntry> enumEntries;
    /// Longest legal String or FilePath value, or StringList item, in bytes; 0 means unbounded.
    /// KeyBinding uses a validated chord syntax independent of this field.
    std::size_t maxLength = 0U;
    /// Maximum number of StringList entries. Required and capped by the registry for that type;
    /// zero for every other type.
    std::size_t maxItems = 0U;
    /// Whether a Color setting carries alpha.
    bool colorHasAlpha = false;
    std::string displayName;
    std::string tooltip;
    /// Where it appears in the settings tree, as a slash path, e.g. "Editor/Viewport/Grid".
    std::string category;
    std::uint32_t flags = kSettingFlagNone;
    /// Schema version in which this setting was introduced (0 = initial baseline).
    std::uint32_t sinceVersion = 0U;
    /// For a Deprecated descriptor, the live setting that replaces this old id. Its stored value
    /// is read through the replacement and migrated to that id on the next document load/save.
    std::string replacementId;

    [[nodiscard]] bool HasFlag(const SettingFlag flag) const noexcept { return (flags & flag) != 0U; }
};

/// Whether `value` is legal for `descriptor`: the right alternative for its type and within every
/// constraint it declares. Not-a-number and infinity are never legal for a number or a channel.
[[nodiscard]] bool IsSettingValueValid(const SettingDescriptor& descriptor, const SettingValue& value);

/// Empty when `descriptor` is well formed; otherwise one sentence saying what is wrong: an id
/// that is not a dot path, a default that breaks the descriptor's own constraints, bounds the
/// wrong way round, an Enum with no entries or with repeated values, and so on.
[[nodiscard]] std::string ValidateSettingDescriptor(const SettingDescriptor& descriptor);

// Declaration helpers, so a setting reads as one line at the place that owns it.
[[nodiscard]] SettingDescriptor MakeBoolSetting(std::string id, bool defaultValue, std::string displayName,
                                                      std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeIntSetting(std::string id, std::int64_t defaultValue,
                                                     std::optional<std::int64_t> minimum,
                                                     std::optional<std::int64_t> maximum, std::string displayName,
                                                     std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeFloatSetting(std::string id, double defaultValue,
                                                       std::optional<double> minimum, std::optional<double> maximum,
                                                       std::string displayName, std::string category,
                                                       std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeStringSetting(std::string id, std::string defaultValue,
                                                        std::size_t maxLength, std::string displayName,
                                                        std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeStringListSetting(std::string id, SettingStringList defaultValue,
                                                            std::size_t maxItems, std::size_t maxLength,
                                                            std::string displayName, std::string category,
                                                            std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeFilePathSetting(std::string id, std::string defaultValue,
                                                          std::size_t maxLength, std::string displayName,
                                                          std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeAssetReferenceSetting(std::string id, std::string defaultValue,
                                                                std::size_t maxLength, std::string displayName,
                                                                std::string category, std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeKeyBindingSetting(std::string id, std::string defaultValue,
                                                            std::string displayName, std::string category,
                                                            std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeEnumSetting(std::string id, std::int64_t defaultValue,
                                                      std::vector<SettingEnumEntry> entries,
                                                      std::string displayName, std::string category,
                                                      std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeLayerMaskSetting(std::string id, std::uint32_t defaultMask,
                                                           std::string displayName, std::string category,
                                                           std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeColorSetting(std::string id, SettingColor defaultValue, bool hasAlpha,
                                                       std::string displayName, std::string category,
                                                       std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeVector2Setting(std::string id, SettingVector2 defaultValue,
                                                         std::optional<double> minimum, std::optional<double> maximum,
                                                         std::string displayName, std::string category,
                                                         std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeVector3Setting(std::string id, SettingVector3 defaultValue,
                                                         std::optional<double> minimum, std::optional<double> maximum,
                                                         std::string displayName, std::string category,
                                                         std::string tooltip = {});
[[nodiscard]] SettingDescriptor MakeVector4Setting(std::string id, SettingVector4 defaultValue,
                                                         std::optional<double> minimum, std::optional<double> maximum,
                                                         std::string displayName, std::string category,
                                                         std::string tooltip = {});

} // namespace UVE::Config
