// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_registry_uve.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace UVE::Config {
namespace {

constexpr double kNoValueUVE = std::numeric_limits<double>::quiet_NaN();

[[nodiscard]] bool IsIdCharacterUVE(const char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// Non-empty segments of letters, digits and underscores, joined by one separator each.
[[nodiscard]] bool IsPathUVE(const std::string_view path, const char separator,
                             const bool allowSpaces) noexcept {
    if (path.empty() || path.front() == separator || path.back() == separator) {
        return false;
    }
    char previous = '\0';
    for (const char c : path) {
        if (c == separator) {
            if (previous == separator) {
                return false;
            }
        } else if (!IsIdCharacterUVE(c) && !(allowSpaces && c == ' ')) {
            return false;
        }
        previous = c;
    }
    return true;
}

[[nodiscard]] bool IsWithinBoundsUVE(const SettingDescriptorUVE& descriptor, const double value) noexcept {
    // Written as "not outside" would let NaN through; "inside" is false for NaN by construction.
    if (!std::isfinite(value)) {
        return false;
    }
    if (descriptor.minimum && !(value >= *descriptor.minimum)) {
        return false;
    }
    if (descriptor.maximum && !(value <= *descriptor.maximum)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool IsChannelValidUVE(const float channel) noexcept {
    return channel >= 0.0F && channel <= 1.0F;
}

[[nodiscard]] bool IsEnumValueValidUVE(const SettingDescriptorUVE& descriptor, const std::int64_t value) {
    return std::any_of(descriptor.enumEntries.begin(), descriptor.enumEntries.end(),
                       [value](const SettingEnumEntryUVE& entry) { return entry.value == value; });
}

[[nodiscard]] bool IsShortcutKeyNameUVE(const std::string_view key) noexcept {
    if (key.size() == 1U) {
        const char character = key.front();
        return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9');
    }
    if (key.size() >= 2U && key.size() <= 3U && key.front() == 'F' && key[1U] != '0') {
        unsigned int number = 0U;
        for (const char character : key.substr(1U)) {
            if (character < '0' || character > '9') {
                return false;
            }
            number = (number * 10U) + static_cast<unsigned int>(character - '0');
        }
        return number >= 1U && number <= 12U;
    }
    static constexpr std::array<std::string_view, 21U> kNamedShortcutKeysUVE{
        "Left", "Right", "Up", "Down", "Delete", "Backspace", "Insert", "Home", "End", "PageUp",
        "PageDown", "Space", "Enter", "Tab", "Comma", "Period", "Slash", "Minus", "Equal", "LeftBracket",
        "RightBracket"};
    return std::find(kNamedShortcutKeysUVE.begin(), kNamedShortcutKeysUVE.end(), key) != kNamedShortcutKeysUVE.end();
}

/// A stable editor shortcut chord: optional unique Ctrl/Shift/Alt modifiers and one known key name.
[[nodiscard]] bool IsKeyBindingValueValidUVE(const std::string_view text) noexcept {
    if (text.empty()) {
        return true; // An empty string is an unbound shortcut.
    }

    bool control = false;
    bool shift = false;
    bool alt = false;
    std::size_t keyStart = 0U;
    while (true) {
        const std::size_t plus = text.find('+', keyStart);
        if (plus == std::string_view::npos) {
            break;
        }
        const std::string_view modifier = text.substr(keyStart, plus - keyStart);
        bool* const seen = modifier == "Ctrl" ? &control : modifier == "Shift" ? &shift
                                                      : modifier == "Alt"     ? &alt
                                                                              : nullptr;
        if (seen == nullptr || *seen || plus + 1U == text.size()) {
            return false;
        }
        *seen = true;
        keyStart = plus + 1U;
    }
    return IsShortcutKeyNameUVE(text.substr(keyStart));
}

/// A colour without alpha is always opaque, whatever `a` was passed.
[[nodiscard]] SettingValueUVE NormalizeUVE(const SettingDescriptorUVE& descriptor, SettingValueUVE value) {
    if (descriptor.type == SettingTypeUVE::Color && !descriptor.colorHasAlpha) {
        if (auto* color = std::get_if<SettingColorUVE>(&value)) {
            color->a = 1.0F;
        }
    }
    return value;
}

[[nodiscard]] std::string ChannelKeyUVE(const std::string& id, const char channel) {
    std::string key;
    key.reserve(id.size() + 2U);
    key.append(id).push_back('.');
    key.push_back(channel);
    return key;
}

[[nodiscard]] std::string StringListCountKeyUVE(const std::string& id) { return id + ".count"; }

[[nodiscard]] std::string StringListItemKeyUVE(const std::string& id, const std::size_t index) {
    return id + "." + std::to_string(index);
}

/// Asset GUIDs share the asset database's fixed-width, lower-case hexadecimal representation.
[[nodiscard]] std::string FormatAssetReferenceGuidInternalUVE(const std::uint64_t sourceGuid) {
    static constexpr std::string_view kHexDigits = "0123456789abcdef";
    std::string text(16U, '0');
    std::uint64_t guid = sourceGuid;
    for (std::size_t index = text.size(); index > 0U; --index) {
        text[index - 1U] = kHexDigits[static_cast<std::size_t>(guid & 0xFU)];
        guid >>= 4U;
    }
    return text;
}

[[nodiscard]] std::optional<std::uint64_t> ParseAssetReferenceGuidInternalUVE(const std::string_view text) noexcept {
    if (text.empty()) {
        return std::uint64_t{0U};
    }
    if (text.size() != 16U) {
        return std::nullopt;
    }
    std::uint64_t guid = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), guid, 16);
    return error == std::errc{} && end == text.data() + text.size() ? std::optional<std::uint64_t>{guid}
                                                                    : std::nullopt;
}

/// The keys beneath a composite setting's id that hold its parts; none for a scalar setting.
[[nodiscard]] std::string_view ComponentKeysUVE(const SettingTypeUVE type) noexcept {
    switch (type) {
    case SettingTypeUVE::Color:
        return "rgba";
    case SettingTypeUVE::Vector2:
        return "xy";
    case SettingTypeUVE::Vector3:
        return "xyz";
    case SettingTypeUVE::Vector4:
        return "xyzw";
    default:
        return {};
    }
}

/// Numeric value from an atomic store snapshot, or NaN when it is missing or not numeric.
[[nodiscard]] double ReadNumberUVE(const std::optional<ConfigScalarValueUVE>& value) {
    if (!value) {
        return kNoValueUVE;
    }
    if (const double* number = std::get_if<double>(&*value)) {
        return *number;
    }
    if (const std::int64_t* integer = std::get_if<std::int64_t>(&*value)) {
        return static_cast<double>(*integer);
    }
    return kNoValueUVE;
}

/// A stored channel as a float, or NaN when it is missing, not a number or outside 0..1. Narrowing
/// only in-range values also keeps a huge stored double from overflowing the float conversion.
[[nodiscard]] float ReadChannelUVE(const std::optional<ConfigScalarValueUVE>& value) {
    const double channel = ReadNumberUVE(value);
    return channel >= 0.0 && channel <= 1.0 ? static_cast<float>(channel) : std::numeric_limits<float>::quiet_NaN();
}

/// Removes a group of paths in one transaction and reports whether any leaf was present before it.
[[nodiscard]] bool ClearPathsUVE(IConfigManagerUVE& store, const std::vector<std::string>& paths) {
    bool hadValue = false;
    std::vector<ConfigMutationUVE> mutations;
    mutations.reserve(paths.size());
    for (const std::string& path : paths) {
        hadValue = store.HasKeyUVE(path) || hadValue;
        mutations.push_back({path, std::nullopt});
    }
    return store.ApplyMutationsUVE(mutations) && hadValue;
}

[[nodiscard]] std::optional<ConfigScalarValueUVE> ReadScalarSettingUVE(const IConfigManagerUVE& store,
                                                                        const std::string& id) {
    const std::vector<std::optional<ConfigScalarValueUVE>> values = store.GetValuesUVE({id});
    return values.empty() ? std::nullopt : values.front();
}

/// Reads the raw stored value of `descriptor`: nothing when it is missing or of the wrong kind.
/// Constraints are checked by the caller.
[[nodiscard]] std::optional<SettingValueUVE> ReadStoredUVE(const SettingDescriptorUVE& descriptor,
                                                          const IConfigManagerUVE& store) {
    const std::string& id = descriptor.id;
    switch (descriptor.type) {
    case SettingTypeUVE::Bool: {
        const std::optional<ConfigScalarValueUVE> stored = ReadScalarSettingUVE(store, id);
        const auto* boolean = stored ? std::get_if<bool>(&*stored) : nullptr;
        return boolean != nullptr ? std::optional<SettingValueUVE>(SettingValueUVE{*boolean}) : std::nullopt;
    }
    case SettingTypeUVE::Int:
    case SettingTypeUVE::Enum: {
        const std::optional<ConfigScalarValueUVE> stored = ReadScalarSettingUVE(store, id);
        if (!stored) {
            return std::nullopt;
        }
        if (const std::int64_t* integer = std::get_if<std::int64_t>(&*stored)) {
            return *integer;
        }
        // A hand-edited file may say 4.0 where 4 is meant; accept a whole number written as one.
        // 2^63 is the first double past the int64 range; the lower bound is exact.
        const double* asDouble = std::get_if<double>(&*stored);
        if (asDouble != nullptr && std::isfinite(*asDouble) && std::trunc(*asDouble) == *asDouble &&
            *asDouble >= -9223372036854775808.0 && *asDouble < 9223372036854775808.0) {
            return static_cast<std::int64_t>(*asDouble);
        }
        return std::nullopt;
    }
    case SettingTypeUVE::Float: {
        const std::optional<ConfigScalarValueUVE> stored = ReadScalarSettingUVE(store, id);
        if (!stored) {
            return std::nullopt;
        }
        if (const double* number = std::get_if<double>(&*stored)) {
            return *number;
        }
        if (const std::int64_t* integer = std::get_if<std::int64_t>(&*stored)) {
            return static_cast<double>(*integer);
        }
        return std::nullopt;
    }
    case SettingTypeUVE::String:
    case SettingTypeUVE::FilePath:
    case SettingTypeUVE::KeyBinding: {
        const std::optional<ConfigScalarValueUVE> stored = ReadScalarSettingUVE(store, id);
        const std::string* text = stored ? std::get_if<std::string>(&*stored) : nullptr;
        return text != nullptr ? std::optional<SettingValueUVE>(SettingValueUVE{*text}) : std::nullopt;
    }
    case SettingTypeUVE::StringList: {
        // Count and members must describe one point-in-time list. Reading each separately could
        // otherwise combine a new count with old values while another thread replaces the list.
        std::vector<std::string> keys;
        keys.reserve(descriptor.maxItems + 1U);
        keys.push_back(StringListCountKeyUVE(id));
        for (std::size_t index = 0U; index < descriptor.maxItems; ++index) {
            keys.push_back(StringListItemKeyUVE(id, index));
        }
        const std::vector<std::optional<ConfigScalarValueUVE>> stored = store.GetValuesUVE(keys);
        if (stored.empty() || !stored.front()) {
            return std::nullopt;
        }
        const auto* storedCount = std::get_if<std::int64_t>(&*stored.front());
        if (storedCount == nullptr || *storedCount < 0) {
            return std::nullopt;
        }
        const std::size_t count = *storedCount > static_cast<std::int64_t>(descriptor.maxItems)
                                      ? descriptor.maxItems
                                      : static_cast<std::size_t>(*storedCount);
        SettingStringListUVE values;
        values.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            // Missing or mistyped entries read as empty, matching the legacy editor lists, whose
            // consumers skip invalid elements without discarding their valid neighbours.
            const auto* text = stored[index + 1U] ? std::get_if<std::string>(&*stored[index + 1U]) : nullptr;
            values.push_back(text != nullptr ? *text : std::string{});
        }
        return values;
    }
    case SettingTypeUVE::Color: {
        std::vector<std::string> keys{ChannelKeyUVE(id, 'r'), ChannelKeyUVE(id, 'g'),
                                      ChannelKeyUVE(id, 'b')};
        if (descriptor.colorHasAlpha) {
            keys.push_back(ChannelKeyUVE(id, 'a'));
        }
        const std::vector<std::optional<ConfigScalarValueUVE>> channels = store.GetValuesUVE(keys);
        if (channels.empty() || !channels.front()) {
            return std::nullopt;
        }
        // Every channel is read in one snapshot. A missing or non-numeric component becomes NaN,
        // so validation takes the entire colour back to its default rather than stitching parts.
        SettingColorUVE color;
        color.r = ReadChannelUVE(channels[0]);
        color.g = ReadChannelUVE(channels[1]);
        color.b = ReadChannelUVE(channels[2]);
        color.a = descriptor.colorHasAlpha ? ReadChannelUVE(channels[3]) : 1.0F;
        return color;
    }
    case SettingTypeUVE::Vector2: {
        const std::vector<std::optional<ConfigScalarValueUVE>> components =
            store.GetValuesUVE({ChannelKeyUVE(id, 'x'), ChannelKeyUVE(id, 'y')});
        if (components.empty() || !components.front()) {
            return std::nullopt;
        }
        return SettingVector2UVE{ReadNumberUVE(components[0]), ReadNumberUVE(components[1])};
    }
    case SettingTypeUVE::Vector3: {
        const std::vector<std::optional<ConfigScalarValueUVE>> components =
            store.GetValuesUVE({ChannelKeyUVE(id, 'x'), ChannelKeyUVE(id, 'y'), ChannelKeyUVE(id, 'z')});
        if (components.empty() || !components.front()) {
            return std::nullopt;
        }
        // Every component comes from the same snapshot and is validated as one vector by the caller.
        return SettingVector3UVE{ReadNumberUVE(components[0]), ReadNumberUVE(components[1]),
                                 ReadNumberUVE(components[2])};
    }
    case SettingTypeUVE::Vector4: {
        const std::vector<std::optional<ConfigScalarValueUVE>> components = store.GetValuesUVE(
            {ChannelKeyUVE(id, 'x'), ChannelKeyUVE(id, 'y'), ChannelKeyUVE(id, 'z'), ChannelKeyUVE(id, 'w')});
        if (components.empty() || !components.front()) {
            return std::nullopt;
        }
        return SettingVector4UVE{ReadNumberUVE(components[0]), ReadNumberUVE(components[1]),
                                 ReadNumberUVE(components[2]), ReadNumberUVE(components[3])};
    }
    case SettingTypeUVE::LayerMask: {
        const std::optional<ConfigScalarValueUVE> stored = ReadScalarSettingUVE(store, id);
        if (!stored) {
            return std::nullopt;
        }
        const double bits = ReadNumberUVE(stored);
        constexpr double kMaximumLayerMaskUVE = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
        if (!std::isfinite(bits) || std::trunc(bits) != bits || bits < 0.0 || bits > kMaximumLayerMaskUVE) {
            return std::nullopt;
        }
        return SettingLayerMaskUVE{static_cast<std::uint32_t>(bits)};
    }
    case SettingTypeUVE::AssetReference: {
        const std::optional<ConfigScalarValueUVE> stored = ReadScalarSettingUVE(store, id);
        const auto* text = stored ? std::get_if<std::string>(&*stored) : nullptr;
        const std::optional<SettingAssetReferenceUVE> reference =
            text != nullptr ? ParseSettingAssetReferenceUVE(*text) : std::nullopt;
        return reference ? std::optional<SettingValueUVE>{*reference} : std::nullopt;
    }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SettingValueUVE> GetValidStoredValueUVE(const SettingDescriptorUVE& descriptor,
                                                                    const IConfigManagerUVE& store) {
    std::optional<SettingValueUVE> stored = ReadStoredUVE(descriptor, store);
    if (!stored || !IsSettingValueValidUVE(descriptor, *stored)) {
        return std::nullopt;
    }
    return NormalizeUVE(descriptor, std::move(*stored));
}

[[nodiscard]] bool ClearDescriptorValueUVE(IConfigManagerUVE& store, const SettingDescriptorUVE& descriptor) {
    if (descriptor.type == SettingTypeUVE::StringList) {
        std::vector<std::string> paths;
        paths.reserve(descriptor.maxItems + 1U);
        paths.push_back(StringListCountKeyUVE(descriptor.id));
        for (std::size_t index = 0U; index < descriptor.maxItems; ++index) {
            paths.push_back(StringListItemKeyUVE(descriptor.id, index));
        }
        return ClearPathsUVE(store, paths);
    }
    const std::string_view components = ComponentKeysUVE(descriptor.type);
    if (components.empty()) {
        return store.RemoveKeyUVE(descriptor.id);
    }
    std::vector<std::string> paths;
    paths.reserve(components.size());
    for (const char component : components) {
        paths.push_back(ChannelKeyUVE(descriptor.id, component));
    }
    return ClearPathsUVE(store, paths);
}

[[nodiscard]] SettingDescriptorUVE MakeSettingUVE(std::string id, const SettingTypeUVE type,
                                                  SettingValueUVE defaultValue, std::string displayName,
                                                  std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor;
    descriptor.id = std::move(id);
    descriptor.type = type;
    descriptor.defaultValue = std::move(defaultValue);
    descriptor.displayName = std::move(displayName);
    descriptor.category = std::move(category);
    descriptor.tooltip = std::move(tooltip);
    return descriptor;
}

[[nodiscard]] std::optional<double> ToBoundUVE(const std::optional<std::int64_t> bound) {
    return bound ? std::optional<double>(static_cast<double>(*bound)) : std::nullopt;
}

} // namespace

std::string FormatSettingAssetReferenceUVE(const SettingAssetReferenceUVE reference) {
    return FormatAssetReferenceGuidInternalUVE(reference.guid);
}

std::optional<SettingAssetReferenceUVE> ParseSettingAssetReferenceUVE(const std::string_view text) noexcept {
    const std::optional<std::uint64_t> guid = ParseAssetReferenceGuidInternalUVE(text);
    return guid ? std::optional<SettingAssetReferenceUVE>{SettingAssetReferenceUVE{*guid}} : std::nullopt;
}

bool IsSettingValueValidUVE(const SettingDescriptorUVE& descriptor, const SettingValueUVE& value) {
    switch (descriptor.type) {
    case SettingTypeUVE::Bool:
        return std::holds_alternative<bool>(value);
    case SettingTypeUVE::Int: {
        const auto* integer = std::get_if<std::int64_t>(&value);
        return integer != nullptr && IsWithinBoundsUVE(descriptor, static_cast<double>(*integer));
    }
    case SettingTypeUVE::Float: {
        const auto* number = std::get_if<double>(&value);
        return number != nullptr && IsWithinBoundsUVE(descriptor, *number);
    }
    case SettingTypeUVE::String: {
        const auto* text = std::get_if<std::string>(&value);
        return text != nullptr && (descriptor.maxLength == 0U || text->size() <= descriptor.maxLength);
    }
    case SettingTypeUVE::FilePath: {
        const auto* path = std::get_if<std::string>(&value);
        return path != nullptr && path->find('\0') == std::string::npos &&
               (descriptor.maxLength == 0U || path->size() <= descriptor.maxLength);
    }
    case SettingTypeUVE::KeyBinding: {
        const auto* binding = std::get_if<std::string>(&value);
        return binding != nullptr && IsKeyBindingValueValidUVE(*binding);
    }
    case SettingTypeUVE::StringList: {
        const auto* list = std::get_if<SettingStringListUVE>(&value);
        if (list == nullptr || descriptor.maxItems == 0U ||
            descriptor.maxItems > kMaximumSettingStringListItemsUVE || list->size() > descriptor.maxItems) {
            return false;
        }
        return descriptor.maxLength == 0U ||
               std::all_of(list->begin(), list->end(), [&descriptor](const std::string& item) {
                   return item.size() <= descriptor.maxLength;
               });
    }
    case SettingTypeUVE::Enum: {
        const auto* integer = std::get_if<std::int64_t>(&value);
        return integer != nullptr && IsEnumValueValidUVE(descriptor, *integer);
    }
    case SettingTypeUVE::Color: {
        const auto* color = std::get_if<SettingColorUVE>(&value);
        return color != nullptr && IsChannelValidUVE(color->r) && IsChannelValidUVE(color->g) &&
               IsChannelValidUVE(color->b) && (!descriptor.colorHasAlpha || IsChannelValidUVE(color->a));
    }
    case SettingTypeUVE::Vector2: {
        const auto* vector = std::get_if<SettingVector2UVE>(&value);
        return vector != nullptr && IsWithinBoundsUVE(descriptor, vector->x) &&
               IsWithinBoundsUVE(descriptor, vector->y);
    }
    case SettingTypeUVE::Vector3: {
        const auto* vector = std::get_if<SettingVector3UVE>(&value);
        return vector != nullptr && IsWithinBoundsUVE(descriptor, vector->x) &&
               IsWithinBoundsUVE(descriptor, vector->y) && IsWithinBoundsUVE(descriptor, vector->z);
    }
    case SettingTypeUVE::Vector4: {
        const auto* vector = std::get_if<SettingVector4UVE>(&value);
        return vector != nullptr && IsWithinBoundsUVE(descriptor, vector->x) &&
               IsWithinBoundsUVE(descriptor, vector->y) && IsWithinBoundsUVE(descriptor, vector->z) &&
               IsWithinBoundsUVE(descriptor, vector->w);
    }
    case SettingTypeUVE::LayerMask:
        return std::holds_alternative<SettingLayerMaskUVE>(value);
    case SettingTypeUVE::AssetReference:
        return std::holds_alternative<SettingAssetReferenceUVE>(value);
    }
    return false;
}

std::string ValidateSettingDescriptorUVE(const SettingDescriptorUVE& descriptor) {
    if (!IsPathUVE(descriptor.id, '.', false)) {
        return "id '" + descriptor.id + "' is not a dot path of letters, digits and underscores";
    }
    if (descriptor.id == kSettingsDocumentVersionKeyUVE || descriptor.id.starts_with("version.")) {
        return "'" + descriptor.id + "' conflicts with the reserved document version key";
    }
    if (descriptor.sinceVersion && *descriptor.sinceVersion == 0U) {
        return "'" + descriptor.id + "' has a sinceVersion of zero; zero is reserved for unversioned documents";
    }
    if (descriptor.HasFlagUVE(kSettingFlagDeprecatedUVE) && !descriptor.migratedFrom.empty()) {
        return "'" + descriptor.id + "' is Deprecated and cannot declare migratedFrom ids";
    }
    std::unordered_set<std::string> migratedFromIds;
    for (const std::string& migratedFrom : descriptor.migratedFrom) {
        if (!IsPathUVE(migratedFrom, '.', false) || migratedFrom == descriptor.id ||
            migratedFrom == kSettingsDocumentVersionKeyUVE || migratedFrom.starts_with("version.")) {
            return "'" + descriptor.id + "' has a malformed, reserved or self-referential migratedFrom id";
        }
        if (!migratedFromIds.insert(migratedFrom).second) {
            return "'" + descriptor.id + "' lists migratedFrom id '" + migratedFrom + "' more than once";
        }
    }
    if (!descriptor.replacementId.empty()) {
        if (!descriptor.HasFlagUVE(kSettingFlagDeprecatedUVE)) {
            return "'" + descriptor.id + "' has a replacement id but is not Deprecated";
        }
        if (!IsPathUVE(descriptor.replacementId, '.', false) || descriptor.replacementId == descriptor.id) {
            return "'" + descriptor.id + "' has a malformed or self-referential replacement id";
        }
    }
    const bool numeric = descriptor.type == SettingTypeUVE::Int || descriptor.type == SettingTypeUVE::Float ||
                         descriptor.type == SettingTypeUVE::Vector2 || descriptor.type == SettingTypeUVE::Vector3 ||
                         descriptor.type == SettingTypeUVE::Vector4;
    if (!numeric && (descriptor.minimum || descriptor.maximum || descriptor.step)) {
        return "'" + descriptor.id + "' declares numeric bounds or a step but is not a numeric setting";
    }
    for (const auto& bound : {descriptor.minimum, descriptor.maximum}) {
        if (bound && !std::isfinite(*bound)) {
            return "'" + descriptor.id + "' has a bound that is not a finite number";
        }
    }
    if (descriptor.minimum && descriptor.maximum && *descriptor.minimum > *descriptor.maximum) {
        return "'" + descriptor.id + "' has a minimum greater than its maximum";
    }
    if (descriptor.step && !(std::isfinite(*descriptor.step) && *descriptor.step > 0.0)) {
        return "'" + descriptor.id + "' has a step that is not a positive finite number";
    }
    if (descriptor.type == SettingTypeUVE::Enum) {
        if (descriptor.enumEntries.empty()) {
            return "'" + descriptor.id + "' is an Enum setting with no entries";
        }
        std::unordered_set<std::int64_t> values;
        std::unordered_set<std::string> labels;
        for (const SettingEnumEntryUVE& entry : descriptor.enumEntries) {
            if (entry.label.empty()) {
                return "'" + descriptor.id + "' has an Enum entry with no label";
            }
            if (!values.insert(entry.value).second || !labels.insert(entry.label).second) {
                return "'" + descriptor.id + "' has two Enum entries with the same value or label";
            }
        }
    } else if (descriptor.type == SettingTypeUVE::StringList) {
        // A StringList may carry entries as the vocabulary its items are chosen from (the editor's
        // per-object default extras do); the owner of the setting enforces membership. The labels
        // are still the items, so they must be non-empty and unique.
        std::unordered_set<std::string> labels;
        for (const SettingEnumEntryUVE& entry : descriptor.enumEntries) {
            if (entry.label.empty()) {
                return "'" + descriptor.id + "' has a StringList entry with no label";
            }
            if (!labels.insert(entry.label).second) {
                return "'" + descriptor.id + "' has two StringList entries with the same label";
            }
        }
    } else if (!descriptor.enumEntries.empty()) {
        return "'" + descriptor.id + "' lists Enum entries but is not an Enum or StringList setting";
    }
    const bool hasStringLength = descriptor.type == SettingTypeUVE::String ||
                                 descriptor.type == SettingTypeUVE::FilePath ||
                                 descriptor.type == SettingTypeUVE::StringList;
    if (!hasStringLength && descriptor.maxLength != 0U) {
        return "'" + descriptor.id + "' declares a maximum length but has no string value";
    }
    if (descriptor.type == SettingTypeUVE::StringList) {
        if (descriptor.maxItems == 0U || descriptor.maxItems > kMaximumSettingStringListItemsUVE) {
            return "'" + descriptor.id + "' has a StringList limit outside 1..4096 items";
        }
    } else if (descriptor.maxItems != 0U) {
        return "'" + descriptor.id + "' declares a maximum item count but is not a StringList setting";
    }
    if (descriptor.type != SettingTypeUVE::Color && descriptor.colorHasAlpha) {
        return "'" + descriptor.id + "' declares alpha but is not a Color setting";
    }
    if (!descriptor.category.empty() && !IsPathUVE(descriptor.category, '/', true)) {
        return "'" + descriptor.id + "' has category '" + descriptor.category +
               "', which is not a slash path of non-empty names";
    }
    if (!IsSettingValueValidUVE(descriptor, descriptor.defaultValue)) {
        return "'" + descriptor.id + "' has a default that is the wrong type or breaks its own constraints";
    }
    return {};
}

SettingDescriptorUVE MakeBoolSettingUVE(std::string id, const bool defaultValue, std::string displayName,
                                        std::string category, std::string tooltip) {
    return MakeSettingUVE(std::move(id), SettingTypeUVE::Bool, defaultValue, std::move(displayName),
                          std::move(category), std::move(tooltip));
}

SettingDescriptorUVE MakeIntSettingUVE(std::string id, const std::int64_t defaultValue,
                                       const std::optional<std::int64_t> minimum,
                                       const std::optional<std::int64_t> maximum, std::string displayName,
                                       std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Int, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = ToBoundUVE(minimum);
    descriptor.maximum = ToBoundUVE(maximum);
    return descriptor;
}

SettingDescriptorUVE MakeFloatSettingUVE(std::string id, const double defaultValue, const std::optional<double> minimum,
                                         const std::optional<double> maximum, std::string displayName,
                                         std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Float, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

SettingDescriptorUVE MakeStringSettingUVE(std::string id, std::string defaultValue, const std::size_t maxLength,
                                          std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor =
        MakeSettingUVE(std::move(id), SettingTypeUVE::String, std::move(defaultValue), std::move(displayName),
                       std::move(category), std::move(tooltip));
    descriptor.maxLength = maxLength;
    return descriptor;
}

SettingDescriptorUVE MakeStringListSettingUVE(std::string id, SettingStringListUVE defaultValue,
                                              const std::size_t maxItems, const std::size_t maxLength,
                                              std::string displayName, std::string category,
                                              std::string tooltip) {
    SettingDescriptorUVE descriptor =
        MakeSettingUVE(std::move(id), SettingTypeUVE::StringList, std::move(defaultValue),
                       std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.maxItems = maxItems;
    descriptor.maxLength = maxLength;
    return descriptor;
}

SettingDescriptorUVE MakeFilePathSettingUVE(std::string id, std::string defaultValue, const std::size_t maxLength,
                                            std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor =
        MakeSettingUVE(std::move(id), SettingTypeUVE::FilePath, std::move(defaultValue), std::move(displayName),
                       std::move(category), std::move(tooltip));
    descriptor.maxLength = maxLength;
    return descriptor;
}

SettingDescriptorUVE MakeKeyBindingSettingUVE(std::string id, std::string defaultValue, std::string displayName,
                                              std::string category, std::string tooltip) {
    return MakeSettingUVE(std::move(id), SettingTypeUVE::KeyBinding, std::move(defaultValue),
                          std::move(displayName), std::move(category), std::move(tooltip));
}

SettingDescriptorUVE MakeEnumSettingUVE(std::string id, const std::int64_t defaultValue,
                                        std::vector<SettingEnumEntryUVE> entries, std::string displayName,
                                        std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Enum, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.enumEntries = std::move(entries);
    return descriptor;
}

SettingDescriptorUVE MakeColorSettingUVE(std::string id, const SettingColorUVE defaultValue, const bool hasAlpha,
                                         std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Color, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.colorHasAlpha = hasAlpha;
    return descriptor;
}

SettingDescriptorUVE MakeVector2SettingUVE(std::string id, const SettingVector2UVE defaultValue,
                                           const std::optional<double> minimum, const std::optional<double> maximum,
                                           std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Vector2, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

SettingDescriptorUVE MakeVector3SettingUVE(std::string id, const SettingVector3UVE defaultValue,
                                           const std::optional<double> minimum, const std::optional<double> maximum,
                                           std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Vector3, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

SettingDescriptorUVE MakeVector4SettingUVE(std::string id, const SettingVector4UVE defaultValue,
                                           const std::optional<double> minimum, const std::optional<double> maximum,
                                           std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Vector4, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

SettingDescriptorUVE MakeLayerMaskSettingUVE(std::string id, const SettingLayerMaskUVE defaultValue,
                                             std::string displayName, std::string category, std::string tooltip) {
    return MakeSettingUVE(std::move(id), SettingTypeUVE::LayerMask, defaultValue, std::move(displayName),
                          std::move(category), std::move(tooltip));
}

SettingDescriptorUVE MakeAssetReferenceSettingUVE(std::string id, const SettingAssetReferenceUVE defaultValue,
                                                  std::string displayName, std::string category,
                                                  std::string tooltip) {
    return MakeSettingUVE(std::move(id), SettingTypeUVE::AssetReference, defaultValue, std::move(displayName),
                          std::move(category), std::move(tooltip));
}

bool SettingsRegistryUVE::RegisterUVE(SettingDescriptorUVE descriptor) {
    if (!ValidateSettingDescriptorUVE(descriptor).empty() || m_byId.contains(descriptor.id)) {
        return false;
    }
    if (!descriptor.replacementId.empty()) {
        const SettingDescriptorUVE* const replacement = FindUVE(descriptor.replacementId);
        if (replacement == nullptr || replacement->HasFlagUVE(kSettingFlagDeprecatedUVE) ||
            replacement->type != descriptor.type ||
            std::find(replacement->migratedFrom.begin(), replacement->migratedFrom.end(), descriptor.id) ==
                replacement->migratedFrom.end()) {
            return false;
        }
    }
    for (const std::string& migratedFrom : descriptor.migratedFrom) {
        const SettingDescriptorUVE* const existingSource = FindUVE(migratedFrom);
        if (existingSource != nullptr &&
            (!existingSource->HasFlagUVE(kSettingFlagDeprecatedUVE) || existingSource->replacementId != descriptor.id)) {
            return false;
        }
        for (const auto& registered : m_descriptors) {
            if (std::find(registered->migratedFrom.begin(), registered->migratedFrom.end(), migratedFrom) !=
                registered->migratedFrom.end()) {
                return false; // One legacy id cannot migrate to two live settings.
            }
        }
    }
    // Where the setting's values sit in the document: at its id, in a composite's component keys,
    // or in a StringList's bounded count/item keys. A composite's id is therefore an object, and
    // other settings may live beside its components, e.g. "outline" and "outline.thickness".
    std::vector<std::string> values;
    if (descriptor.type == SettingTypeUVE::StringList) {
        values.reserve(descriptor.maxItems + 1U);
        values.push_back(StringListCountKeyUVE(descriptor.id));
        for (std::size_t index = 0U; index < descriptor.maxItems; ++index) {
            values.push_back(StringListItemKeyUVE(descriptor.id, index));
        }
    } else {
        for (const char component : ComponentKeysUVE(descriptor.type)) {
            values.push_back(ChannelKeyUVE(descriptor.id, component));
        }
        if (values.empty()) {
            values.push_back(descriptor.id);
        }
    }
    // No path may be both a value and an object: "a.b" holding a value rules out "a.b.c", and the
    // other way round.
    std::vector<std::string> branches;
    for (const std::string& value : values) {
        if (m_values.contains(value) || m_branches.contains(value)) {
            return false;
        }
        for (std::size_t dot = value.find('.'); dot != std::string::npos; dot = value.find('.', dot + 1U)) {
            branches.push_back(value.substr(0U, dot));
            if (m_values.contains(branches.back())) {
                return false;
            }
        }
    }
    m_values.insert(std::make_move_iterator(values.begin()), std::make_move_iterator(values.end()));
    m_branches.insert(std::make_move_iterator(branches.begin()), std::make_move_iterator(branches.end()));
    descriptor.defaultValue = NormalizeUVE(descriptor, std::move(descriptor.defaultValue));
    auto owned = std::make_unique<SettingDescriptorUVE>(std::move(descriptor));
    m_byId.emplace(owned->id, owned.get());
    m_descriptors.push_back(std::move(owned));
    return true;
}

const SettingDescriptorUVE* SettingsRegistryUVE::FindUVE(const std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    return it != m_byId.end() ? it->second : nullptr;
}

std::vector<const SettingDescriptorUVE*> SettingsRegistryUVE::GetAllUVE() const {
    std::vector<const SettingDescriptorUVE*> all;
    all.reserve(m_descriptors.size());
    for (const auto& descriptor : m_descriptors) {
        all.push_back(descriptor.get());
    }
    return all;
}

std::optional<SettingValueUVE> SettingsRegistryUVE::GetValueUVE(const IConfigManagerUVE& store,
                                                               const std::string_view id) const {
    const SettingDescriptorUVE* descriptor = FindUVE(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    std::optional<SettingValueUVE> stored = GetStoredValueUVE(store, id);
    return stored ? std::move(stored) : std::optional<SettingValueUVE>(descriptor->defaultValue);
}

bool SettingsRegistryUVE::SetValueUVE(IConfigManagerUVE& store, const std::string_view id,
                                      const SettingValueUVE& value) const {
    const SettingDescriptorUVE* descriptor = FindUVE(id);
    if (descriptor == nullptr || descriptor->HasFlagUVE(kSettingFlagDeprecatedUVE) ||
        !IsSettingValueValidUVE(*descriptor, value)) {
        return false;
    }
    const std::string& key = descriptor->id;
    switch (descriptor->type) {
    case SettingTypeUVE::Bool:
        store.SetBoolUVE(key, std::get<bool>(value));
        break;
    case SettingTypeUVE::Int:
    case SettingTypeUVE::Enum:
        store.SetIntUVE(key, std::get<std::int64_t>(value));
        break;
    case SettingTypeUVE::Float:
        store.SetDoubleUVE(key, std::get<double>(value));
        break;
    case SettingTypeUVE::String:
    case SettingTypeUVE::FilePath:
    case SettingTypeUVE::KeyBinding:
        store.SetStringUVE(key, std::get<std::string>(value));
        break;
    case SettingTypeUVE::StringList: {
        const SettingStringListUVE& list = std::get<SettingStringListUVE>(value);
        std::vector<ConfigMutationUVE> mutations;
        mutations.reserve((descriptor->maxItems * 2U) + 1U);
        for (std::size_t index = 0U; index < descriptor->maxItems; ++index) {
            mutations.push_back({StringListItemKeyUVE(key, index), std::nullopt});
        }
        mutations.push_back({StringListCountKeyUVE(key),
                             ConfigScalarValueUVE{static_cast<std::int64_t>(list.size())}});
        for (std::size_t index = 0U; index < list.size(); ++index) {
            mutations.push_back({StringListItemKeyUVE(key, index), ConfigScalarValueUVE{list[index]}});
        }
        if (!store.ApplyMutationsUVE(mutations)) {
            return false;
        }
        break;
    }
    case SettingTypeUVE::Color: {
        const auto& color = std::get<SettingColorUVE>(value);
        std::vector<ConfigMutationUVE> mutations{
            {ChannelKeyUVE(key, 'r'), ConfigScalarValueUVE{static_cast<double>(color.r)}},
            {ChannelKeyUVE(key, 'g'), ConfigScalarValueUVE{static_cast<double>(color.g)}},
            {ChannelKeyUVE(key, 'b'), ConfigScalarValueUVE{static_cast<double>(color.b)}}};
        if (descriptor->colorHasAlpha) {
            mutations.push_back(
                {ChannelKeyUVE(key, 'a'), ConfigScalarValueUVE{static_cast<double>(color.a)}});
        }
        if (!store.ApplyMutationsUVE(mutations)) {
            return false;
        }
        break;
    }
    case SettingTypeUVE::Vector2: {
        const auto& vector = std::get<SettingVector2UVE>(value);
        const std::vector<ConfigMutationUVE> mutations{
            {ChannelKeyUVE(key, 'x'), ConfigScalarValueUVE{vector.x}},
            {ChannelKeyUVE(key, 'y'), ConfigScalarValueUVE{vector.y}}};
        if (!store.ApplyMutationsUVE(mutations)) {
            return false;
        }
        break;
    }
    case SettingTypeUVE::Vector3: {
        const auto& vector = std::get<SettingVector3UVE>(value);
        const std::vector<ConfigMutationUVE> mutations{
            {ChannelKeyUVE(key, 'x'), ConfigScalarValueUVE{vector.x}},
            {ChannelKeyUVE(key, 'y'), ConfigScalarValueUVE{vector.y}},
            {ChannelKeyUVE(key, 'z'), ConfigScalarValueUVE{vector.z}}};
        if (!store.ApplyMutationsUVE(mutations)) {
            return false;
        }
        break;
    }
    case SettingTypeUVE::Vector4: {
        const auto& vector = std::get<SettingVector4UVE>(value);
        const std::vector<ConfigMutationUVE> mutations{
            {ChannelKeyUVE(key, 'x'), ConfigScalarValueUVE{vector.x}},
            {ChannelKeyUVE(key, 'y'), ConfigScalarValueUVE{vector.y}},
            {ChannelKeyUVE(key, 'z'), ConfigScalarValueUVE{vector.z}},
            {ChannelKeyUVE(key, 'w'), ConfigScalarValueUVE{vector.w}}};
        if (!store.ApplyMutationsUVE(mutations)) {
            return false;
        }
        break;
    }
    case SettingTypeUVE::LayerMask:
        store.SetIntUVE(key, static_cast<std::int64_t>(std::get<SettingLayerMaskUVE>(value).bits));
        break;
    case SettingTypeUVE::AssetReference:
        store.SetStringUVE(key, FormatSettingAssetReferenceUVE(std::get<SettingAssetReferenceUVE>(value)));
        break;
    }
    for (const auto& candidate : m_descriptors) {
        if (candidate->HasFlagUVE(kSettingFlagDeprecatedUVE) && candidate->replacementId == descriptor->id) {
            static_cast<void>(ClearDescriptorValueUVE(store, *candidate));
        }
    }
    return true;
}

bool SettingsRegistryUVE::ResetUVE(IConfigManagerUVE& store, const std::string_view id) const {
    const SettingDescriptorUVE* descriptor = FindUVE(id);
    return descriptor != nullptr && SetValueUVE(store, id, descriptor->defaultValue);
}

bool SettingsRegistryUVE::ClearValueUVE(IConfigManagerUVE& store, const std::string_view id) const {
    const SettingDescriptorUVE* descriptor = FindUVE(id);
    if (descriptor == nullptr) {
        return false;
    }
    bool removed = ClearDescriptorValueUVE(store, *descriptor);
    if (!descriptor->HasFlagUVE(kSettingFlagDeprecatedUVE)) {
        for (const auto& candidate : m_descriptors) {
            if (candidate->HasFlagUVE(kSettingFlagDeprecatedUVE) && candidate->replacementId == descriptor->id) {
                removed = ClearDescriptorValueUVE(store, *candidate) || removed;
            }
        }
    }
    return removed;
}

std::optional<SettingValueUVE> SettingsRegistryUVE::GetStoredValueUVE(const IConfigManagerUVE& store,
                                                                     const std::string_view id) const {
    const SettingDescriptorUVE* descriptor = FindUVE(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    if (std::optional<SettingValueUVE> stored = GetValidStoredValueUVE(*descriptor, store)) {
        return stored;
    }
    if (descriptor->HasFlagUVE(kSettingFlagDeprecatedUVE)) {
        return std::nullopt;
    }
    for (const auto& alias : m_descriptors) {
        if (!alias->HasFlagUVE(kSettingFlagDeprecatedUVE) || alias->replacementId != descriptor->id) {
            continue;
        }
        const std::optional<SettingValueUVE> legacy = GetValidStoredValueUVE(*alias, store);
        if (legacy && IsSettingValueValidUVE(*descriptor, *legacy)) {
            return NormalizeUVE(*descriptor, *legacy);
        }
    }
    return std::nullopt;
}

bool SettingsRegistryUVE::MigrateDeprecatedValuesUVE(IConfigManagerUVE& store) const {
    bool changed = false;
    for (const auto& alias : m_descriptors) {
        if (!alias->HasFlagUVE(kSettingFlagDeprecatedUVE) || alias->replacementId.empty()) {
            continue;
        }
        const SettingDescriptorUVE* const replacement = FindUVE(alias->replacementId);
        if (replacement == nullptr || replacement->HasFlagUVE(kSettingFlagDeprecatedUVE)) {
            continue; // registration prevents this; remain defensive if the registry changes later
        }
        const std::optional<SettingValueUVE> current = GetValidStoredValueUVE(*replacement, store);
        const std::optional<SettingValueUVE> legacy = GetValidStoredValueUVE(*alias, store);
        if (current) {
            changed = ClearDescriptorValueUVE(store, *alias) || changed;
        } else if (legacy && IsSettingValueValidUVE(*replacement, *legacy)) {
            if (NormalizeUVE(*replacement, *legacy) == replacement->defaultValue) {
                changed = ClearValueUVE(store, replacement->id) || changed;
            } else if (SetValueUVE(store, replacement->id, NormalizeUVE(*replacement, *legacy))) {
                changed = true;
            }
        }
    }
    return changed;
}

bool SettingsRegistryUVE::IsModifiedUVE(const IConfigManagerUVE& store, const std::string_view id) const {
    const SettingDescriptorUVE* descriptor = FindUVE(id);
    if (descriptor == nullptr) {
        return false;
    }
    return GetValueUVE(store, id) != descriptor->defaultValue;
}

namespace {

template <typename T>
[[nodiscard]] T GetTypedUVE(const SettingsRegistryUVE& registry, const IConfigManagerUVE& store,
                            const std::string_view id, std::initializer_list<SettingTypeUVE> types, T fallback) {
    const SettingDescriptorUVE* descriptor = registry.FindUVE(id);
    if (descriptor == nullptr || std::find(types.begin(), types.end(), descriptor->type) == types.end()) {
        return fallback;
    }
    const std::optional<SettingValueUVE> value = registry.GetValueUVE(store, id);
    const T* typed = value ? std::get_if<T>(&*value) : nullptr;
    return typed != nullptr ? *typed : fallback;
}

} // namespace

bool SettingsRegistryUVE::GetBoolUVE(const IConfigManagerUVE& store, const std::string_view id,
                                     const bool fallback) const {
    return GetTypedUVE<bool>(*this, store, id, {SettingTypeUVE::Bool}, fallback);
}

std::int64_t SettingsRegistryUVE::GetIntUVE(const IConfigManagerUVE& store, const std::string_view id,
                                            const std::int64_t fallback) const {
    return GetTypedUVE<std::int64_t>(*this, store, id, {SettingTypeUVE::Int, SettingTypeUVE::Enum}, fallback);
}

double SettingsRegistryUVE::GetFloatUVE(const IConfigManagerUVE& store, const std::string_view id,
                                        const double fallback) const {
    return GetTypedUVE<double>(*this, store, id, {SettingTypeUVE::Float}, fallback);
}

std::string SettingsRegistryUVE::GetStringUVE(const IConfigManagerUVE& store, const std::string_view id,
                                              std::string fallback) const {
    return GetTypedUVE<std::string>(*this, store, id,
                                    {SettingTypeUVE::String, SettingTypeUVE::FilePath, SettingTypeUVE::KeyBinding},
                                    std::move(fallback));
}

SettingStringListUVE SettingsRegistryUVE::GetStringListUVE(const IConfigManagerUVE& store,
                                                            const std::string_view id,
                                                            SettingStringListUVE fallback) const {
    return GetTypedUVE<SettingStringListUVE>(*this, store, id, {SettingTypeUVE::StringList}, std::move(fallback));
}

SettingColorUVE SettingsRegistryUVE::GetColorUVE(const IConfigManagerUVE& store, const std::string_view id,
                                                 const SettingColorUVE fallback) const {
    return GetTypedUVE<SettingColorUVE>(*this, store, id, {SettingTypeUVE::Color}, fallback);
}

SettingVector2UVE SettingsRegistryUVE::GetVector2UVE(const IConfigManagerUVE& store, const std::string_view id,
                                                     const SettingVector2UVE fallback) const {
    return GetTypedUVE<SettingVector2UVE>(*this, store, id, {SettingTypeUVE::Vector2}, fallback);
}

SettingVector3UVE SettingsRegistryUVE::GetVector3UVE(const IConfigManagerUVE& store, const std::string_view id,
                                                     const SettingVector3UVE fallback) const {
    return GetTypedUVE<SettingVector3UVE>(*this, store, id, {SettingTypeUVE::Vector3}, fallback);
}

SettingVector4UVE SettingsRegistryUVE::GetVector4UVE(const IConfigManagerUVE& store, const std::string_view id,
                                                     const SettingVector4UVE fallback) const {
    return GetTypedUVE<SettingVector4UVE>(*this, store, id, {SettingTypeUVE::Vector4}, fallback);
}

SettingLayerMaskUVE SettingsRegistryUVE::GetLayerMaskUVE(const IConfigManagerUVE& store, const std::string_view id,
                                                         const SettingLayerMaskUVE fallback) const {
    return GetTypedUVE<SettingLayerMaskUVE>(*this, store, id, {SettingTypeUVE::LayerMask}, fallback);
}

SettingAssetReferenceUVE SettingsRegistryUVE::GetAssetReferenceUVE(const IConfigManagerUVE& store,
                                                                   const std::string_view id,
                                                                   const SettingAssetReferenceUVE fallback) const {
    return GetTypedUVE<SettingAssetReferenceUVE>(*this, store, id, {SettingTypeUVE::AssetReference}, fallback);
}

} // namespace UVE::Config
