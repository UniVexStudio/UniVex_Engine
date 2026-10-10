// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_registry_uve.h"

#include <algorithm>
#include <array>
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

/// The keys beneath a composite setting's id that hold its parts; none for a scalar setting.
[[nodiscard]] std::string_view ComponentKeysUVE(const SettingTypeUVE type) noexcept {
    switch (type) {
    case SettingTypeUVE::Color:
        return "rgba";
    case SettingTypeUVE::Vector3:
        return "xyz";
    default:
        return {};
    }
}

/// A stored channel as a float, or NaN when it is missing, not a number or outside 0..1. Narrowing
/// only in-range values also keeps a huge stored double from overflowing the float conversion.
[[nodiscard]] float ReadChannelUVE(const IConfigManagerUVE& store, const std::string& key) {
    const double channel = store.GetDoubleUVE(key, kNoValueUVE);
    return channel >= 0.0 && channel <= 1.0 ? static_cast<float>(channel) : std::numeric_limits<float>::quiet_NaN();
}

/// Reads the raw stored value of `descriptor`: nothing when it is missing or of the wrong kind.
/// Constraints are checked by the caller.
[[nodiscard]] std::optional<SettingValueUVE> ReadStoredUVE(const SettingDescriptorUVE& descriptor,
                                                          const IConfigManagerUVE& store) {
    const std::string& id = descriptor.id;
    if (descriptor.type == SettingTypeUVE::StringList) {
        if (!store.HasKeyUVE(StringListCountKeyUVE(id))) {
            return std::nullopt;
        }
    } else if (ComponentKeysUVE(descriptor.type).empty() && !store.HasKeyUVE(id)) {
        return std::nullopt;
    }
    switch (descriptor.type) {
    case SettingTypeUVE::Bool: {
        // Two reads with opposite fallbacks agree only when a bool is really there.
        const bool stored = store.GetBoolUVE(id, false);
        return stored == store.GetBoolUVE(id, true) ? std::optional<SettingValueUVE>(stored) : std::nullopt;
    }
    case SettingTypeUVE::Int:
    case SettingTypeUVE::Enum: {
        constexpr auto kSentinel = std::numeric_limits<std::int64_t>::min();
        const std::int64_t stored = store.GetIntUVE(id, kSentinel);
        if (stored != kSentinel || store.GetIntUVE(id, 0) == kSentinel) {
            return stored;
        }
        // A hand-edited file may say 4.0 where 4 is meant; accept a whole number written as one.
        // 2^63 is the first double past the int64 range; the lower bound is exact.
        const double asDouble = store.GetDoubleUVE(id, kNoValueUVE);
        if (std::isfinite(asDouble) && std::trunc(asDouble) == asDouble && asDouble >= -9223372036854775808.0 &&
            asDouble < 9223372036854775808.0) {
            return static_cast<std::int64_t>(asDouble);
        }
        return std::nullopt;
    }
    case SettingTypeUVE::Float: {
        // NaN never reaches the document as a number, so reading it back means "not a number".
        const double stored = store.GetDoubleUVE(id, kNoValueUVE);
        return std::isnan(stored) ? std::nullopt : std::optional<SettingValueUVE>(stored);
    }
    case SettingTypeUVE::String:
    case SettingTypeUVE::FilePath:
    case SettingTypeUVE::KeyBinding: {
        std::string stored = store.GetStringUVE(id, "");
        // An empty read is a real empty string only if a second, non-empty fallback reads empty too.
        if (stored.empty() && !store.GetStringUVE(id, "x").empty()) {
            return std::nullopt;
        }
        return SettingValueUVE{std::move(stored)};
    }
    case SettingTypeUVE::StringList: {
        constexpr std::int64_t kMissingListCountUVE = std::numeric_limits<std::int64_t>::min();
        const std::int64_t storedCount = store.GetIntUVE(StringListCountKeyUVE(id), kMissingListCountUVE);
        if (storedCount == kMissingListCountUVE || storedCount < 0) {
            return std::nullopt;
        }
        const std::size_t count = storedCount > static_cast<std::int64_t>(descriptor.maxItems)
                                      ? descriptor.maxItems
                                      : static_cast<std::size_t>(storedCount);
        SettingStringListUVE values;
        values.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            // Missing or mistyped entries read as empty, matching the legacy editor lists, whose
            // consumers skip invalid elements without discarding their valid neighbours.
            values.push_back(store.GetStringUVE(StringListItemKeyUVE(id, index), ""));
        }
        return values;
    }
    case SettingTypeUVE::Color: {
        if (!store.HasKeyUVE(ChannelKeyUVE(id, 'r'))) {
            return std::nullopt;
        }
        // A missing or non-numeric channel reads as NaN, which fails validation and so takes the
        // whole colour back to its default - never a colour stitched from stored and default parts.
        SettingColorUVE color;
        color.r = ReadChannelUVE(store, ChannelKeyUVE(id, 'r'));
        color.g = ReadChannelUVE(store, ChannelKeyUVE(id, 'g'));
        color.b = ReadChannelUVE(store, ChannelKeyUVE(id, 'b'));
        color.a = descriptor.colorHasAlpha ? ReadChannelUVE(store, ChannelKeyUVE(id, 'a')) : 1.0F;
        return color;
    }
    case SettingTypeUVE::Vector3: {
        if (!store.HasKeyUVE(ChannelKeyUVE(id, 'x'))) {
            return std::nullopt;
        }
        // As with a colour, a missing or non-numeric component reads as NaN and fails the vector.
        return SettingVector3UVE{store.GetDoubleUVE(ChannelKeyUVE(id, 'x'), kNoValueUVE),
                                 store.GetDoubleUVE(ChannelKeyUVE(id, 'y'), kNoValueUVE),
                                 store.GetDoubleUVE(ChannelKeyUVE(id, 'z'), kNoValueUVE)};
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
        bool removed = store.RemoveKeyUVE(StringListCountKeyUVE(descriptor.id));
        for (std::size_t index = 0U; index < descriptor.maxItems; ++index) {
            removed = store.RemoveKeyUVE(StringListItemKeyUVE(descriptor.id, index)) || removed;
        }
        return removed;
    }
    const std::string_view components = ComponentKeysUVE(descriptor.type);
    if (components.empty()) {
        return store.RemoveKeyUVE(descriptor.id);
    }
    bool removed = false;
    for (const char component : components) {
        removed = store.RemoveKeyUVE(ChannelKeyUVE(descriptor.id, component)) || removed;
    }
    return removed;
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
    case SettingTypeUVE::Vector3: {
        const auto* vector = std::get_if<SettingVector3UVE>(&value);
        return vector != nullptr && IsWithinBoundsUVE(descriptor, vector->x) &&
               IsWithinBoundsUVE(descriptor, vector->y) && IsWithinBoundsUVE(descriptor, vector->z);
    }
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
    if (!descriptor.replacementId.empty()) {
        if (!descriptor.HasFlagUVE(kSettingFlagDeprecatedUVE)) {
            return "'" + descriptor.id + "' has a replacement id but is not Deprecated";
        }
        if (!IsPathUVE(descriptor.replacementId, '.', false) || descriptor.replacementId == descriptor.id) {
            return "'" + descriptor.id + "' has a malformed or self-referential replacement id";
        }
    }
    const bool numeric = descriptor.type == SettingTypeUVE::Int || descriptor.type == SettingTypeUVE::Float ||
                         descriptor.type == SettingTypeUVE::Vector3;
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

SettingDescriptorUVE MakeVector3SettingUVE(std::string id, const SettingVector3UVE defaultValue,
                                           const std::optional<double> minimum, const std::optional<double> maximum,
                                           std::string displayName, std::string category, std::string tooltip) {
    SettingDescriptorUVE descriptor = MakeSettingUVE(std::move(id), SettingTypeUVE::Vector3, defaultValue,
                                                     std::move(displayName), std::move(category), std::move(tooltip));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

bool SettingsRegistryUVE::RegisterUVE(SettingDescriptorUVE descriptor) {
    if (!ValidateSettingDescriptorUVE(descriptor).empty() || m_byId.contains(descriptor.id)) {
        return false;
    }
    if (!descriptor.replacementId.empty()) {
        const SettingDescriptorUVE* const replacement = FindUVE(descriptor.replacementId);
        if (replacement == nullptr || replacement->HasFlagUVE(kSettingFlagDeprecatedUVE) ||
            replacement->type != descriptor.type) {
            return false;
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
        for (std::size_t index = 0U; index < descriptor->maxItems; ++index) {
            static_cast<void>(store.RemoveKeyUVE(StringListItemKeyUVE(key, index)));
        }
        store.SetIntUVE(StringListCountKeyUVE(key), static_cast<std::int64_t>(list.size()));
        for (std::size_t index = 0U; index < list.size(); ++index) {
            store.SetStringUVE(StringListItemKeyUVE(key, index), list[index]);
        }
        break;
    }
    case SettingTypeUVE::Color: {
        // Every channel was validated above, so the colour is written whole.
        const auto& color = std::get<SettingColorUVE>(value);
        store.SetDoubleUVE(ChannelKeyUVE(key, 'r'), color.r);
        store.SetDoubleUVE(ChannelKeyUVE(key, 'g'), color.g);
        store.SetDoubleUVE(ChannelKeyUVE(key, 'b'), color.b);
        if (descriptor->colorHasAlpha) {
            store.SetDoubleUVE(ChannelKeyUVE(key, 'a'), color.a);
        }
        break;
    }
    case SettingTypeUVE::Vector3: {
        const auto& vector = std::get<SettingVector3UVE>(value);
        store.SetDoubleUVE(ChannelKeyUVE(key, 'x'), vector.x);
        store.SetDoubleUVE(ChannelKeyUVE(key, 'y'), vector.y);
        store.SetDoubleUVE(ChannelKeyUVE(key, 'z'), vector.z);
        break;
    }
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

SettingVector3UVE SettingsRegistryUVE::GetVector3UVE(const IConfigManagerUVE& store, const std::string_view id,
                                                     const SettingVector3UVE fallback) const {
    return GetTypedUVE<SettingVector3UVE>(*this, store, id, {SettingTypeUVE::Vector3}, fallback);
}

} // namespace UVE::Config
