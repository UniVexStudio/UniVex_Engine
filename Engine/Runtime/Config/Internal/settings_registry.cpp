// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "settings_registry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace UVE::Config {
namespace {

constexpr double kNoValue = std::numeric_limits<double>::quiet_NaN();

[[nodiscard]] bool IsIdCharacter(const char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// Non-empty segments of letters, digits and underscores, joined by one separator each.
[[nodiscard]] bool IsPath(const std::string_view path, const char separator,
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
        } else if (!IsIdCharacter(c) && !(allowSpaces && c == ' ')) {
            return false;
        }
        previous = c;
    }
    return true;
}

[[nodiscard]] bool IsWithinBounds(const SettingDescriptor& descriptor, const double value) noexcept {
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

[[nodiscard]] bool IsChannelValid(const float channel) noexcept {
    return channel >= 0.0F && channel <= 1.0F;
}

[[nodiscard]] bool IsEnumValueValid(const SettingDescriptor& descriptor, const std::int64_t value) {
    return std::any_of(descriptor.enumEntries.begin(), descriptor.enumEntries.end(),
                       [value](const SettingEnumEntry& entry) { return entry.value == value; });
}

[[nodiscard]] bool IsShortcutKeyName(const std::string_view key) noexcept {
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
    static constexpr std::array<std::string_view, 21U> kNamedShortcutKeys{
        "Left", "Right", "Up", "Down", "Delete", "Backspace", "Insert", "Home", "End", "PageUp",
        "PageDown", "Space", "Enter", "Tab", "Comma", "Period", "Slash", "Minus", "Equal", "LeftBracket",
        "RightBracket"};
    return std::find(kNamedShortcutKeys.begin(), kNamedShortcutKeys.end(), key) != kNamedShortcutKeys.end();
}

/// A stable editor shortcut chord: optional unique Ctrl/Shift/Alt modifiers and one known key name.
[[nodiscard]] bool IsKeyBindingValueValid(const std::string_view text) noexcept {
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
    return IsShortcutKeyName(text.substr(keyStart));
}

/// A colour without alpha is always opaque, whatever `a` was passed.
[[nodiscard]] SettingValue Normalize(const SettingDescriptor& descriptor, SettingValue value) {
    if (descriptor.type == SettingType::Color && !descriptor.colorHasAlpha) {
        if (auto* color = std::get_if<SettingColor>(&value)) {
            color->a = 1.0F;
        }
    }
    return value;
}

[[nodiscard]] std::string ChannelKey(const std::string& id, const char channel) {
    std::string key;
    key.reserve(id.size() + 2U);
    key.append(id).push_back('.');
    key.push_back(channel);
    return key;
}

[[nodiscard]] std::string StringListCountKey(const std::string& id) { return id + ".count"; }

[[nodiscard]] std::string StringListItemKey(const std::string& id, const std::size_t index) {
    return id + "." + std::to_string(index);
}

/// The keys beneath a composite setting's id that hold its parts; none for a scalar setting.
[[nodiscard]] std::string_view ComponentKeys(const SettingType type) noexcept {
    switch (type) {
    case SettingType::Color:
        return "rgba";
    case SettingType::Vector2:
        return "xy";
    case SettingType::Vector3:
        return "xyz";
    case SettingType::Vector4:
        return "xyzw";
    default:
        return {};
    }
}

/// A stored channel as a float, or NaN when it is missing, not a number or outside 0..1. Narrowing
/// only in-range values also keeps a huge stored double from overflowing the float conversion.
[[nodiscard]] float ReadChannel(const IConfigManager& store, const std::string& key) {
    const double channel = store.GetDouble(key, kNoValue);
    return channel >= 0.0 && channel <= 1.0 ? static_cast<float>(channel) : std::numeric_limits<float>::quiet_NaN();
}

/// Reads the raw stored value of `descriptor`: nothing when it is missing or of the wrong kind.
/// Constraints are checked by the caller.
[[nodiscard]] std::optional<SettingValue> ReadStored(const SettingDescriptor& descriptor,
                                                          const IConfigManager& store) {
    const std::string& id = descriptor.id;
    if (descriptor.type == SettingType::StringList) {
        if (!store.HasKey(StringListCountKey(id))) {
            return std::nullopt;
        }
    } else if (ComponentKeys(descriptor.type).empty() && !store.HasKey(id)) {
        return std::nullopt;
    }
    switch (descriptor.type) {
    case SettingType::Bool: {
        // Two reads with opposite fallbacks agree only when a bool is really there.
        const bool stored = store.GetBool(id, false);
        return stored == store.GetBool(id, true) ? std::optional<SettingValue>(stored) : std::nullopt;
    }
    case SettingType::Int:
    case SettingType::Enum:
    case SettingType::LayerMask: {
        constexpr auto kSentinel = std::numeric_limits<std::int64_t>::min();
        const std::int64_t stored = store.GetInt(id, kSentinel);
        if (stored != kSentinel || store.GetInt(id, 0) == kSentinel) {
            return stored;
        }
        // A hand-edited file may say 4.0 where 4 is meant; accept a whole number written as one.
        // 2^63 is the first double past the int64 range; the lower bound is exact.
        const double asDouble = store.GetDouble(id, kNoValue);
        if (std::isfinite(asDouble) && std::trunc(asDouble) == asDouble && asDouble >= -9223372036854775808.0 &&
            asDouble < 9223372036854775808.0) {
            return static_cast<std::int64_t>(asDouble);
        }
        return std::nullopt;
    }
    case SettingType::Float: {
        // NaN never reaches the document as a number, so reading it back means "not a number".
        const double stored = store.GetDouble(id, kNoValue);
        return std::isnan(stored) ? std::nullopt : std::optional<SettingValue>(stored);
    }
    case SettingType::String:
    case SettingType::FilePath:
    case SettingType::AssetReference:
    case SettingType::KeyBinding: {
        std::string stored = store.GetString(id, "");
        // An empty read is a real empty string only if a second, non-empty fallback reads empty too.
        if (stored.empty() && !store.GetString(id, "x").empty()) {
            return std::nullopt;
        }
        return SettingValue{std::move(stored)};
    }
    case SettingType::StringList: {
        constexpr std::int64_t kMissingListCount = std::numeric_limits<std::int64_t>::min();
        const std::int64_t storedCount = store.GetInt(StringListCountKey(id), kMissingListCount);
        if (storedCount == kMissingListCount || storedCount < 0) {
            return std::nullopt;
        }
        const std::size_t count = storedCount > static_cast<std::int64_t>(descriptor.maxItems)
                                      ? descriptor.maxItems
                                      : static_cast<std::size_t>(storedCount);
        SettingStringList values;
        values.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            // Missing or mistyped entries read as empty, matching the legacy editor lists, whose
            // consumers skip invalid elements without discarding their valid neighbours.
            values.push_back(store.GetString(StringListItemKey(id, index), ""));
        }
        return values;
    }
    case SettingType::Color: {
        if (!store.HasKey(ChannelKey(id, 'r'))) {
            return std::nullopt;
        }
        // A missing or non-numeric channel reads as NaN, which fails validation and so takes the
        // whole colour back to its default - never a colour stitched from stored and default parts.
        SettingColor color;
        color.r = ReadChannel(store, ChannelKey(id, 'r'));
        color.g = ReadChannel(store, ChannelKey(id, 'g'));
        color.b = ReadChannel(store, ChannelKey(id, 'b'));
        color.a = descriptor.colorHasAlpha ? ReadChannel(store, ChannelKey(id, 'a')) : 1.0F;
        return color;
    }
    case SettingType::Vector2: {
        if (!store.HasKey(ChannelKey(id, 'x')) || !store.HasKey(ChannelKey(id, 'y'))) {
            return std::nullopt;
        }
        return SettingVector2{store.GetDouble(ChannelKey(id, 'x'), kNoValue),
                                 store.GetDouble(ChannelKey(id, 'y'), kNoValue)};
    }
    case SettingType::Vector3: {
        if (!store.HasKey(ChannelKey(id, 'x')) || !store.HasKey(ChannelKey(id, 'y')) ||
            !store.HasKey(ChannelKey(id, 'z'))) {
            return std::nullopt;
        }
        // As with a colour, a missing or non-numeric component reads as NaN and fails the vector.
        return SettingVector3{store.GetDouble(ChannelKey(id, 'x'), kNoValue),
                                 store.GetDouble(ChannelKey(id, 'y'), kNoValue),
                                 store.GetDouble(ChannelKey(id, 'z'), kNoValue)};
    }
    case SettingType::Vector4: {
        if (!store.HasKey(ChannelKey(id, 'x')) || !store.HasKey(ChannelKey(id, 'y')) ||
            !store.HasKey(ChannelKey(id, 'z')) || !store.HasKey(ChannelKey(id, 'w'))) {
            return std::nullopt;
        }
        return SettingVector4{store.GetDouble(ChannelKey(id, 'x'), kNoValue),
                                 store.GetDouble(ChannelKey(id, 'y'), kNoValue),
                                 store.GetDouble(ChannelKey(id, 'z'), kNoValue),
                                 store.GetDouble(ChannelKey(id, 'w'), kNoValue)};
    }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SettingValue> GetValidStoredValue(const SettingDescriptor& descriptor,
                                                                    const IConfigManager& store) {
    std::optional<SettingValue> stored = ReadStored(descriptor, store);
    if (!stored || !IsSettingValueValid(descriptor, *stored)) {
        return std::nullopt;
    }
    return Normalize(descriptor, std::move(*stored));
}

[[nodiscard]] bool ClearDescriptorValue(IConfigManager& store, const SettingDescriptor& descriptor) {
    if (descriptor.type == SettingType::StringList) {
        bool removed = store.RemoveKey(StringListCountKey(descriptor.id));
        for (std::size_t index = 0U; index < descriptor.maxItems; ++index) {
            removed = store.RemoveKey(StringListItemKey(descriptor.id, index)) || removed;
        }
        return removed;
    }
    const std::string_view components = ComponentKeys(descriptor.type);
    if (components.empty()) {
        return store.RemoveKey(descriptor.id);
    }
    bool removed = false;
    for (const char component : components) {
        removed = store.RemoveKey(ChannelKey(descriptor.id, component)) || removed;
    }
    return removed;
}

[[nodiscard]] SettingDescriptor MakeSetting(std::string id, const SettingType type,
                                                  SettingValue defaultValue, std::string displayName,
                                                  std::string category, std::string tooltip) {
    SettingDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = type;
    descriptor.defaultValue = std::move(defaultValue);
    descriptor.displayName = std::move(displayName);
    descriptor.category = std::move(category);
    descriptor.tooltip = std::move(tooltip);
    return descriptor;
}

[[nodiscard]] std::optional<double> ToBound(const std::optional<std::int64_t> bound) {
    return bound ? std::optional<double>(static_cast<double>(*bound)) : std::nullopt;
}

} // namespace

bool IsSettingValueValid(const SettingDescriptor& descriptor, const SettingValue& value) {
    switch (descriptor.type) {
    case SettingType::Bool:
        return std::holds_alternative<bool>(value);
    case SettingType::Int: {
        const auto* integer = std::get_if<std::int64_t>(&value);
        return integer != nullptr && IsWithinBounds(descriptor, static_cast<double>(*integer));
    }
    case SettingType::Float: {
        const auto* number = std::get_if<double>(&value);
        return number != nullptr && IsWithinBounds(descriptor, *number);
    }
    case SettingType::String: {
        const auto* text = std::get_if<std::string>(&value);
        return text != nullptr && (descriptor.maxLength == 0U || text->size() <= descriptor.maxLength);
    }
    case SettingType::FilePath: {
        const auto* path = std::get_if<std::string>(&value);
        return path != nullptr && path->find('\0') == std::string::npos &&
               (descriptor.maxLength == 0U || path->size() <= descriptor.maxLength);
    }
    case SettingType::AssetReference: {
        const auto* ref = std::get_if<std::string>(&value);
        return ref != nullptr && ref->find('\0') == std::string::npos &&
               ref->find("..") == std::string::npos &&
               (descriptor.maxLength == 0U || ref->size() <= descriptor.maxLength);
    }
    case SettingType::KeyBinding: {
        const auto* binding = std::get_if<std::string>(&value);
        return binding != nullptr && IsKeyBindingValueValid(*binding);
    }
    case SettingType::StringList: {
        const auto* list = std::get_if<SettingStringList>(&value);
        if (list == nullptr || descriptor.maxItems == 0U ||
            descriptor.maxItems > kMaximumSettingStringListItems || list->size() > descriptor.maxItems) {
            return false;
        }
        return descriptor.maxLength == 0U ||
               std::all_of(list->begin(), list->end(), [&descriptor](const std::string& item) {
                   return item.size() <= descriptor.maxLength;
               });
    }
    case SettingType::Enum: {
        const auto* integer = std::get_if<std::int64_t>(&value);
        return integer != nullptr && IsEnumValueValid(descriptor, *integer);
    }
    case SettingType::LayerMask: {
        const auto* mask = std::get_if<std::int64_t>(&value);
        return mask != nullptr && *mask >= 0 && *mask <= 0xFFFFFFFFLL;
    }
    case SettingType::Color: {
        const auto* color = std::get_if<SettingColor>(&value);
        return color != nullptr && IsChannelValid(color->r) && IsChannelValid(color->g) &&
               IsChannelValid(color->b) && (!descriptor.colorHasAlpha || IsChannelValid(color->a));
    }
    case SettingType::Vector2: {
        const auto* vector = std::get_if<SettingVector2>(&value);
        return vector != nullptr && IsWithinBounds(descriptor, vector->x) &&
               IsWithinBounds(descriptor, vector->y);
    }
    case SettingType::Vector3: {
        const auto* vector = std::get_if<SettingVector3>(&value);
        return vector != nullptr && IsWithinBounds(descriptor, vector->x) &&
               IsWithinBounds(descriptor, vector->y) && IsWithinBounds(descriptor, vector->z);
    }
    case SettingType::Vector4: {
        const auto* vector = std::get_if<SettingVector4>(&value);
        return vector != nullptr && IsWithinBounds(descriptor, vector->x) &&
               IsWithinBounds(descriptor, vector->y) && IsWithinBounds(descriptor, vector->z) &&
               IsWithinBounds(descriptor, vector->w);
    }
    }
    return false;
}

std::string ValidateSettingDescriptor(const SettingDescriptor& descriptor) {
    if (!IsPath(descriptor.id, '.', false)) {
        return "id '" + descriptor.id + "' is not a dot path of letters, digits and underscores";
    }
    if (descriptor.id == kSettingsDocumentVersionKey || descriptor.id.starts_with("version.")) {
        return "'" + descriptor.id + "' conflicts with the reserved document version key";
    }
    if (!descriptor.replacementId.empty()) {
        if (!descriptor.HasFlag(kSettingFlagDeprecated)) {
            return "'" + descriptor.id + "' has a replacement id but is not Deprecated";
        }
        if (!IsPath(descriptor.replacementId, '.', false) || descriptor.replacementId == descriptor.id) {
            return "'" + descriptor.id + "' has a malformed or self-referential replacement id";
        }
    }
    const bool numeric = descriptor.type == SettingType::Int || descriptor.type == SettingType::Float ||
                         descriptor.type == SettingType::Vector2 || descriptor.type == SettingType::Vector3 ||
                         descriptor.type == SettingType::Vector4;
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
    if (descriptor.type == SettingType::Enum) {
        if (descriptor.enumEntries.empty()) {
            return "'" + descriptor.id + "' is an Enum setting with no entries";
        }
        std::unordered_set<std::int64_t> values;
        std::unordered_set<std::string> labels;
        for (const SettingEnumEntry& entry : descriptor.enumEntries) {
            if (entry.label.empty()) {
                return "'" + descriptor.id + "' has an Enum entry with no label";
            }
            if (!values.insert(entry.value).second || !labels.insert(entry.label).second) {
                return "'" + descriptor.id + "' has two Enum entries with the same value or label";
            }
        }
    } else if (!descriptor.enumEntries.empty()) {
        return "'" + descriptor.id + "' lists Enum entries but is not an Enum setting";
    }
    const bool hasStringLength = descriptor.type == SettingType::String ||
                                 descriptor.type == SettingType::FilePath ||
                                 descriptor.type == SettingType::AssetReference ||
                                 descriptor.type == SettingType::StringList;
    if (!hasStringLength && descriptor.maxLength != 0U) {
        return "'" + descriptor.id + "' declares a maximum length but has no string value";
    }
    if (descriptor.type == SettingType::StringList) {
        if (descriptor.maxItems == 0U || descriptor.maxItems > kMaximumSettingStringListItems) {
            return "'" + descriptor.id + "' has a StringList limit outside 1..4096 items";
        }
    } else if (descriptor.maxItems != 0U) {
        return "'" + descriptor.id + "' declares a maximum item count but is not a StringList setting";
    }
    if (descriptor.type != SettingType::Color && descriptor.colorHasAlpha) {
        return "'" + descriptor.id + "' declares alpha but is not a Color setting";
    }
    if (!descriptor.category.empty() && !IsPath(descriptor.category, '/', true)) {
        return "'" + descriptor.id + "' has category '" + descriptor.category +
               "', which is not a slash path of non-empty names";
    }
    if (!IsSettingValueValid(descriptor, descriptor.defaultValue)) {
        return "'" + descriptor.id + "' has a default that is the wrong type or breaks its own constraints";
    }
    return {};
}

SettingDescriptor MakeBoolSetting(std::string id, const bool defaultValue, std::string displayName,
                                        std::string category, std::string tooltip) {
    return MakeSetting(std::move(id), SettingType::Bool, defaultValue, std::move(displayName),
                          std::move(category), std::move(tooltip));
}

SettingDescriptor MakeIntSetting(std::string id, const std::int64_t defaultValue,
                                       const std::optional<std::int64_t> minimum,
                                       const std::optional<std::int64_t> maximum, std::string displayName,
                                       std::string category, std::string tooltip) {
    SettingDescriptor descriptor = MakeSetting(std::move(id), SettingType::Int, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = ToBound(minimum);
    descriptor.maximum = ToBound(maximum);
    return descriptor;
}

SettingDescriptor MakeFloatSetting(std::string id, const double defaultValue, const std::optional<double> minimum,
                                         const std::optional<double> maximum, std::string displayName,
                                         std::string category, std::string tooltip) {
    SettingDescriptor descriptor = MakeSetting(std::move(id), SettingType::Float, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

SettingDescriptor MakeStringSetting(std::string id, std::string defaultValue, const std::size_t maxLength,
                                          std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptor descriptor =
        MakeSetting(std::move(id), SettingType::String, std::move(defaultValue), std::move(displayName),
                       std::move(category), std::move(tooltip));
    descriptor.maxLength = maxLength;
    return descriptor;
}

SettingDescriptor MakeStringListSetting(std::string id, SettingStringList defaultValue,
                                              const std::size_t maxItems, const std::size_t maxLength,
                                              std::string displayName, std::string category,
                                              std::string tooltip) {
    SettingDescriptor descriptor =
        MakeSetting(std::move(id), SettingType::StringList, std::move(defaultValue),
                       std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.maxItems = maxItems;
    descriptor.maxLength = maxLength;
    return descriptor;
}

SettingDescriptor MakeFilePathSetting(std::string id, std::string defaultValue, const std::size_t maxLength,
                                            std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptor descriptor =
        MakeSetting(std::move(id), SettingType::FilePath, std::move(defaultValue), std::move(displayName),
                       std::move(category), std::move(tooltip));
    descriptor.maxLength = maxLength;
    return descriptor;
}

SettingDescriptor MakeAssetReferenceSetting(std::string id, std::string defaultValue,
                                                  const std::size_t maxLength, std::string displayName,
                                                  std::string category, std::string tooltip) {
    SettingDescriptor descriptor =
        MakeSetting(std::move(id), SettingType::AssetReference, std::move(defaultValue),
                       std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.maxLength = maxLength;
    return descriptor;
}

SettingDescriptor MakeKeyBindingSetting(std::string id, std::string defaultValue, std::string displayName,
                                              std::string category, std::string tooltip) {
    return MakeSetting(std::move(id), SettingType::KeyBinding, std::move(defaultValue),
                          std::move(displayName), std::move(category), std::move(tooltip));
}

SettingDescriptor MakeEnumSetting(std::string id, const std::int64_t defaultValue,
                                        std::vector<SettingEnumEntry> entries, std::string displayName,
                                        std::string category, std::string tooltip) {
    SettingDescriptor descriptor = MakeSetting(std::move(id), SettingType::Enum, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.enumEntries = std::move(entries);
    return descriptor;
}

SettingDescriptor MakeLayerMaskSetting(std::string id, const std::uint32_t defaultMask,
                                             std::string displayName, std::string category,
                                             std::string tooltip) {
    return MakeSetting(std::move(id), SettingType::LayerMask,
                          static_cast<std::int64_t>(defaultMask), std::move(displayName),
                          std::move(category), std::move(tooltip));
}

SettingDescriptor MakeColorSetting(std::string id, const SettingColor defaultValue, const bool hasAlpha,
                                         std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptor descriptor = MakeSetting(std::move(id), SettingType::Color, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.colorHasAlpha = hasAlpha;
    return descriptor;
}

SettingDescriptor MakeVector2Setting(std::string id, const SettingVector2 defaultValue,
                                           const std::optional<double> minimum, const std::optional<double> maximum,
                                           std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptor descriptor = MakeSetting(std::move(id), SettingType::Vector2, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

SettingDescriptor MakeVector3Setting(std::string id, const SettingVector3 defaultValue,
                                           const std::optional<double> minimum, const std::optional<double> maximum,
                                           std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptor descriptor = MakeSetting(std::move(id), SettingType::Vector3, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

SettingDescriptor MakeVector4Setting(std::string id, const SettingVector4 defaultValue,
                                           const std::optional<double> minimum, const std::optional<double> maximum,
                                           std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptor descriptor = MakeSetting(std::move(id), SettingType::Vector4, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

bool SettingsRegistry::Register(SettingDescriptor descriptor) {
    if (!ValidateSettingDescriptor(descriptor).empty() || m_byId.contains(descriptor.id)) {
        return false;
    }
    if (!descriptor.replacementId.empty()) {
        const SettingDescriptor* const replacement = Find(descriptor.replacementId);
        if (replacement == nullptr || replacement->HasFlag(kSettingFlagDeprecated) ||
            replacement->type != descriptor.type) {
            return false;
        }
    }
    // Where the setting's values sit in the document: at its id, in a composite's component keys,
    // or in a StringList's bounded count/item keys. A composite's id is therefore an object, and
    // other settings may live beside its components, e.g. "outline" and "outline.thickness".
    std::vector<std::string> values;
    if (descriptor.type == SettingType::StringList) {
        values.reserve(descriptor.maxItems + 1U);
        values.push_back(StringListCountKey(descriptor.id));
        for (std::size_t index = 0U; index < descriptor.maxItems; ++index) {
            values.push_back(StringListItemKey(descriptor.id, index));
        }
    } else {
        for (const char component : ComponentKeys(descriptor.type)) {
            values.push_back(ChannelKey(descriptor.id, component));
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
    descriptor.defaultValue = Normalize(descriptor, std::move(descriptor.defaultValue));
    auto owned = std::make_unique<SettingDescriptor>(std::move(descriptor));
    m_byId.emplace(owned->id, owned.get());
    m_descriptors.push_back(std::move(owned));
    return true;
}

const SettingDescriptor* SettingsRegistry::Find(const std::string_view id) const {
    const auto it = m_byId.find(std::string(id));
    return it != m_byId.end() ? it->second : nullptr;
}

std::vector<const SettingDescriptor*> SettingsRegistry::GetAll() const {
    std::vector<const SettingDescriptor*> all;
    all.reserve(m_descriptors.size());
    for (const auto& descriptor : m_descriptors) {
        all.push_back(descriptor.get());
    }
    return all;
}

std::optional<SettingValue> SettingsRegistry::GetValue(const IConfigManager& store,
                                                               const std::string_view id) const {
    const SettingDescriptor* descriptor = Find(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    std::optional<SettingValue> stored = GetStoredValue(store, id);
    return stored ? std::move(stored) : std::optional<SettingValue>(descriptor->defaultValue);
}

bool SettingsRegistry::SetValue(IConfigManager& store, const std::string_view id,
                                      const SettingValue& value) const {
    const SettingDescriptor* descriptor = Find(id);
    if (descriptor == nullptr || descriptor->HasFlag(kSettingFlagDeprecated) ||
        !IsSettingValueValid(*descriptor, value)) {
        return false;
    }
    const std::string& key = descriptor->id;
    switch (descriptor->type) {
    case SettingType::Bool:
        store.SetBool(key, std::get<bool>(value));
        break;
    case SettingType::Int:
    case SettingType::Enum:
    case SettingType::LayerMask:
        store.SetInt(key, std::get<std::int64_t>(value));
        break;
    case SettingType::Float:
        store.SetDouble(key, std::get<double>(value));
        break;
    case SettingType::String:
    case SettingType::FilePath:
    case SettingType::AssetReference:
    case SettingType::KeyBinding:
        store.SetString(key, std::get<std::string>(value));
        break;
    case SettingType::StringList: {
        const SettingStringList& list = std::get<SettingStringList>(value);
        for (std::size_t index = 0U; index < descriptor->maxItems; ++index) {
            static_cast<void>(store.RemoveKey(StringListItemKey(key, index)));
        }
        store.SetInt(StringListCountKey(key), static_cast<std::int64_t>(list.size()));
        for (std::size_t index = 0U; index < list.size(); ++index) {
            store.SetString(StringListItemKey(key, index), list[index]);
        }
        break;
    }
    case SettingType::Color: {
        // Every channel was validated above, so the colour is written whole.
        const auto& color = std::get<SettingColor>(value);
        store.SetDouble(ChannelKey(key, 'r'), color.r);
        store.SetDouble(ChannelKey(key, 'g'), color.g);
        store.SetDouble(ChannelKey(key, 'b'), color.b);
        if (descriptor->colorHasAlpha) {
            store.SetDouble(ChannelKey(key, 'a'), color.a);
        }
        break;
    }
    case SettingType::Vector2: {
        const auto& vector = std::get<SettingVector2>(value);
        store.SetDouble(ChannelKey(key, 'x'), vector.x);
        store.SetDouble(ChannelKey(key, 'y'), vector.y);
        break;
    }
    case SettingType::Vector3: {
        const auto& vector = std::get<SettingVector3>(value);
        store.SetDouble(ChannelKey(key, 'x'), vector.x);
        store.SetDouble(ChannelKey(key, 'y'), vector.y);
        store.SetDouble(ChannelKey(key, 'z'), vector.z);
        break;
    }
    case SettingType::Vector4: {
        const auto& vector = std::get<SettingVector4>(value);
        store.SetDouble(ChannelKey(key, 'x'), vector.x);
        store.SetDouble(ChannelKey(key, 'y'), vector.y);
        store.SetDouble(ChannelKey(key, 'z'), vector.z);
        store.SetDouble(ChannelKey(key, 'w'), vector.w);
        break;
    }
    }
    for (const auto& candidate : m_descriptors) {
        if (candidate->HasFlag(kSettingFlagDeprecated) && candidate->replacementId == descriptor->id) {
            static_cast<void>(ClearDescriptorValue(store, *candidate));
        }
    }
    return true;
}

bool SettingsRegistry::Reset(IConfigManager& store, const std::string_view id) const {
    const SettingDescriptor* descriptor = Find(id);
    return descriptor != nullptr && SetValue(store, id, descriptor->defaultValue);
}

bool SettingsRegistry::ClearValue(IConfigManager& store, const std::string_view id) const {
    const SettingDescriptor* descriptor = Find(id);
    if (descriptor == nullptr) {
        return false;
    }
    bool removed = ClearDescriptorValue(store, *descriptor);
    if (!descriptor->HasFlag(kSettingFlagDeprecated)) {
        for (const auto& candidate : m_descriptors) {
            if (candidate->HasFlag(kSettingFlagDeprecated) && candidate->replacementId == descriptor->id) {
                removed = ClearDescriptorValue(store, *candidate) || removed;
            }
        }
    }
    return removed;
}

std::optional<SettingValue> SettingsRegistry::GetStoredValue(const IConfigManager& store,
                                                                     const std::string_view id) const {
    const SettingDescriptor* descriptor = Find(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    if (std::optional<SettingValue> stored = GetValidStoredValue(*descriptor, store)) {
        return stored;
    }
    if (descriptor->HasFlag(kSettingFlagDeprecated)) {
        return std::nullopt;
    }
    for (const auto& alias : m_descriptors) {
        if (!alias->HasFlag(kSettingFlagDeprecated) || alias->replacementId != descriptor->id) {
            continue;
        }
        const std::optional<SettingValue> legacy = GetValidStoredValue(*alias, store);
        if (legacy && IsSettingValueValid(*descriptor, *legacy)) {
            return Normalize(*descriptor, *legacy);
        }
    }
    return std::nullopt;
}

bool SettingsRegistry::MigrateDeprecatedValues(IConfigManager& store) const {
    bool changed = false;
    for (const auto& alias : m_descriptors) {
        if (!alias->HasFlag(kSettingFlagDeprecated) || alias->replacementId.empty()) {
            continue;
        }
        const SettingDescriptor* const replacement = Find(alias->replacementId);
        if (replacement == nullptr || replacement->HasFlag(kSettingFlagDeprecated)) {
            continue; // registration prevents this; remain defensive if the registry changes later
        }
        const std::optional<SettingValue> current = GetValidStoredValue(*replacement, store);
        const std::optional<SettingValue> legacy = GetValidStoredValue(*alias, store);
        if (current) {
            changed = ClearDescriptorValue(store, *alias) || changed;
        } else if (legacy && IsSettingValueValid(*replacement, *legacy)) {
            if (Normalize(*replacement, *legacy) == replacement->defaultValue) {
                changed = ClearValue(store, replacement->id) || changed;
            } else if (SetValue(store, replacement->id, Normalize(*replacement, *legacy))) {
                changed = true;
            }
        }
    }
    return changed;
}

bool SettingsRegistry::IsModified(const IConfigManager& store, const std::string_view id) const {
    const SettingDescriptor* descriptor = Find(id);
    if (descriptor == nullptr) {
        return false;
    }
    return GetValue(store, id) != descriptor->defaultValue;
}

namespace {

template <typename T>
[[nodiscard]] T GetTyped(const SettingsRegistry& registry, const IConfigManager& store,
                            const std::string_view id, std::initializer_list<SettingType> types, T fallback) {
    const SettingDescriptor* descriptor = registry.Find(id);
    if (descriptor == nullptr || std::find(types.begin(), types.end(), descriptor->type) == types.end()) {
        return fallback;
    }
    const std::optional<SettingValue> value = registry.GetValue(store, id);
    const T* typed = value ? std::get_if<T>(&*value) : nullptr;
    return typed != nullptr ? *typed : fallback;
}

} // namespace

bool SettingsRegistry::GetBool(const IConfigManager& store, const std::string_view id,
                                     const bool fallback) const {
    return GetTyped<bool>(*this, store, id, {SettingType::Bool}, fallback);
}

std::int64_t SettingsRegistry::GetInt(const IConfigManager& store, const std::string_view id,
                                            const std::int64_t fallback) const {
    return GetTyped<std::int64_t>(
        *this, store, id, {SettingType::Int, SettingType::Enum, SettingType::LayerMask}, fallback);
}

double SettingsRegistry::GetFloat(const IConfigManager& store, const std::string_view id,
                                        const double fallback) const {
    return GetTyped<double>(*this, store, id, {SettingType::Float}, fallback);
}

std::string SettingsRegistry::GetString(const IConfigManager& store, const std::string_view id,
                                              std::string fallback) const {
    return GetTyped<std::string>(*this, store, id,
                                    {SettingType::String, SettingType::FilePath,
                                     SettingType::AssetReference, SettingType::KeyBinding},
                                    std::move(fallback));
}

SettingStringList SettingsRegistry::GetStringList(const IConfigManager& store,
                                                            const std::string_view id,
                                                            SettingStringList fallback) const {
    return GetTyped<SettingStringList>(*this, store, id, {SettingType::StringList}, std::move(fallback));
}

SettingColor SettingsRegistry::GetColor(const IConfigManager& store, const std::string_view id,
                                                 const SettingColor fallback) const {
    return GetTyped<SettingColor>(*this, store, id, {SettingType::Color}, fallback);
}

std::uint32_t SettingsRegistry::GetLayerMask(const IConfigManager& store,
                                                   const std::string_view id,
                                                   const std::uint32_t fallback) const {
    const std::int64_t value =
        GetTyped<std::int64_t>(*this, store, id, {SettingType::LayerMask}, static_cast<std::int64_t>(fallback));
    return static_cast<std::uint32_t>(value & 0xFFFFFFFFLL);
}

SettingVector2 SettingsRegistry::GetVector2(const IConfigManager& store,
                                                     const std::string_view id,
                                                     const SettingVector2 fallback) const {
    return GetTyped<SettingVector2>(*this, store, id, {SettingType::Vector2}, fallback);
}

SettingVector3 SettingsRegistry::GetVector3(const IConfigManager& store, const std::string_view id,
                                                     const SettingVector3 fallback) const {
    return GetTyped<SettingVector3>(*this, store, id, {SettingType::Vector3}, fallback);
}

SettingVector4 SettingsRegistry::GetVector4(const IConfigManager& store,
                                                     const std::string_view id,
                                                     const SettingVector4 fallback) const {
    return GetTyped<SettingVector4>(*this, store, id, {SettingType::Vector4}, fallback);
}

} // namespace UVE::Config
