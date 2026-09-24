// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_registry_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
    if (descriptor.type != SettingTypeUVE::Color && !store.HasKeyUVE(id)) {
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
    case SettingTypeUVE::String: {
        std::string stored = store.GetStringUVE(id, "");
        // An empty read is a real empty string only if a second, non-empty fallback reads empty too.
        if (stored.empty() && !store.GetStringUVE(id, "x").empty()) {
            return std::nullopt;
        }
        return SettingValueUVE{std::move(stored)};
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
    }
    return std::nullopt;
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
    case SettingTypeUVE::Enum: {
        const auto* integer = std::get_if<std::int64_t>(&value);
        return integer != nullptr && IsEnumValueValidUVE(descriptor, *integer);
    }
    case SettingTypeUVE::Color: {
        const auto* color = std::get_if<SettingColorUVE>(&value);
        return color != nullptr && IsChannelValidUVE(color->r) && IsChannelValidUVE(color->g) &&
               IsChannelValidUVE(color->b) && (!descriptor.colorHasAlpha || IsChannelValidUVE(color->a));
    }
    }
    return false;
}

std::string ValidateSettingDescriptorUVE(const SettingDescriptorUVE& descriptor) {
    if (!IsPathUVE(descriptor.id, '.', false)) {
        return "id '" + descriptor.id + "' is not a dot path of letters, digits and underscores";
    }
    const bool numeric = descriptor.type == SettingTypeUVE::Int || descriptor.type == SettingTypeUVE::Float;
    if (!numeric && (descriptor.minimum || descriptor.maximum || descriptor.step)) {
        return "'" + descriptor.id + "' declares numeric bounds or a step but is not an Int or Float setting";
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
    } else if (!descriptor.enumEntries.empty()) {
        return "'" + descriptor.id + "' lists Enum entries but is not an Enum setting";
    }
    if (descriptor.type != SettingTypeUVE::String && descriptor.maxLength != 0U) {
        return "'" + descriptor.id + "' declares a maximum length but is not a String setting";
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

bool SettingsRegistryUVE::RegisterUVE(SettingDescriptorUVE descriptor) {
    if (!ValidateSettingDescriptorUVE(descriptor).empty() || m_byId.contains(descriptor.id)) {
        return false;
    }
    // Where the setting's values sit in the document: at its id, or for a colour at its four
    // channel keys (alpha reserved even when unused). A colour's id is therefore an object, and
    // other settings may live beside its channels, e.g. "outline" and "outline.thickness".
    std::vector<std::string> values;
    if (descriptor.type == SettingTypeUVE::Color) {
        for (const char channel : {'r', 'g', 'b', 'a'}) {
            values.push_back(ChannelKeyUVE(descriptor.id, channel));
        }
    } else {
        values.push_back(descriptor.id);
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
        store.SetStringUVE(key, std::get<std::string>(value));
        break;
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
    if (descriptor->type != SettingTypeUVE::Color) {
        return store.RemoveKeyUVE(descriptor->id);
    }
    bool removed = false;
    for (const char channel : {'r', 'g', 'b', 'a'}) {
        removed = store.RemoveKeyUVE(ChannelKeyUVE(descriptor->id, channel)) || removed;
    }
    return removed;
}

std::optional<SettingValueUVE> SettingsRegistryUVE::GetStoredValueUVE(const IConfigManagerUVE& store,
                                                                     const std::string_view id) const {
    const SettingDescriptorUVE* descriptor = FindUVE(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }
    std::optional<SettingValueUVE> stored = ReadStoredUVE(*descriptor, store);
    if (!stored || !IsSettingValueValidUVE(*descriptor, *stored)) {
        return std::nullopt;
    }
    return NormalizeUVE(*descriptor, std::move(*stored));
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
    return GetTypedUVE<std::string>(*this, store, id, {SettingTypeUVE::String}, std::move(fallback));
}

SettingColorUVE SettingsRegistryUVE::GetColorUVE(const IConfigManagerUVE& store, const std::string_view id,
                                                 const SettingColorUVE fallback) const {
    return GetTypedUVE<SettingColorUVE>(*this, store, id, {SettingTypeUVE::Color}, fallback);
}

} // namespace UVE::Config
