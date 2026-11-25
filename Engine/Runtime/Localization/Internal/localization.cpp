// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "localization.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include <nlohmann/json.hpp>

namespace UVE::Localization {
namespace {

[[nodiscard]] bool IsAsciiAlpha(const char character) noexcept {
    return std::isalpha(static_cast<unsigned char>(character)) != 0;
}

[[nodiscard]] bool IsAlphaSubtag(const std::string_view subtag) noexcept {
    return !subtag.empty() && subtag.size() <= kMaximumLocaleSubtagBytes &&
           std::all_of(subtag.cbegin(), subtag.cend(), IsAsciiAlpha);
}

[[nodiscard]] bool IsNumericRegionSubtag(const std::string_view subtag) noexcept {
    return subtag.size() == 3U &&
           std::all_of(subtag.cbegin(), subtag.cend(), [](const char character) noexcept {
               return std::isdigit(static_cast<unsigned char>(character)) != 0;
           });
}

[[nodiscard]] bool IsRegionOrScriptSubtag(const std::string_view subtag) noexcept {
    return IsAlphaSubtag(subtag) || IsNumericRegionSubtag(subtag);
}

[[nodiscard]] std::string ToLower(const std::string_view value) {
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

[[nodiscard]] std::string ToUpper(const std::string_view value) {
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](const char character) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    });
    return result;
}

} // namespace

std::string Locale::ToTag() const {
    return region.empty() ? language : language + "-" + region;
}

std::optional<Locale> TryParseLocale(const std::string_view tag) {
    if (tag.empty() || tag.size() > (kMaximumLocaleSubtagBytes * 3U) + 2U) {
        return std::nullopt;
    }
    // Both separators are accepted because both are in circulation - BCP 47 writes "en-US" while
    // POSIX environments write "en_US", and a tag read from either place should mean the same
    // locale rather than silently becoming one nothing has translations for.
    const std::size_t firstSeparator = tag.find_first_of("-_");
    const std::string_view language = tag.substr(0U, firstSeparator);
    if (!IsAlphaSubtag(language)) {
        return std::nullopt;
    }
    if (firstSeparator == std::string_view::npos) {
        return Locale{ToLower(language), ""};
    }
    const std::string_view remainder = tag.substr(firstSeparator + 1U);
    if (remainder.empty()) {
        return std::nullopt;
    }
    const std::size_t secondSeparator = remainder.find_first_of("-_");
    if (secondSeparator == std::string_view::npos) {
        if (!IsRegionOrScriptSubtag(remainder)) {
            return std::nullopt;
        }
        return Locale{ToLower(language), ToUpper(remainder)};
    }
    const std::string_view scriptSubtag = remainder.substr(0U, secondSeparator);
    const std::string_view regionSubtag = remainder.substr(secondSeparator + 1U);
    if (!IsAlphaSubtag(scriptSubtag) || !IsRegionOrScriptSubtag(regionSubtag) ||
        regionSubtag.find_first_of("-_") != std::string_view::npos) {
        return std::nullopt;
    }
    return Locale{ToLower(language), ToUpper(scriptSubtag) + "-" + ToUpper(regionSubtag)};
}

bool StringTable::SetTranslation(std::string key, std::string value) {
    if (key.empty() || key.size() > kMaximumTranslationKeyBytes ||
        value.size() > kMaximumTranslationValueBytes) {
        return false;
    }
    const auto existing = std::find_if(m_translations.begin(), m_translations.end(),
                                       [&key](const Translation& translation) {
                                           return translation.key == key;
                                       });
    if (existing != m_translations.end()) {
        existing->value = std::move(value);
        return true;
    }
    if (m_translations.size() >= kMaximumTranslationsPerTable) {
        return false;
    }
    m_translations.push_back(Translation{std::move(key), std::move(value)});
    return true;
}

const std::string* StringTable::FindTranslation(const std::string_view key) const noexcept {
    const auto iterator = std::find_if(m_translations.cbegin(), m_translations.cend(),
                                       [key](const Translation& translation) {
                                           return translation.key == key;
                                       });
    return iterator == m_translations.cend() ? nullptr : &iterator->value;
}

std::optional<StringTable> TryParseStringTableJson(Locale locale, const std::string_view json) {
    // Parsed without exceptions: a malformed translation file is an expected input, not an
    // exceptional one, and the caller is told through the empty result.
    const nlohmann::json document = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object() ||
        document.size() > kMaximumTranslationsPerTable) {
        return std::nullopt;
    }
    StringTable table{std::move(locale)};
    for (const auto& [key, value] : document.items()) {
        if (!value.is_string() || !table.SetTranslation(key, value.get<std::string>())) {
            return std::nullopt;
        }
    }
    return table;
}

bool LocalizationService::AddStringTable(StringTable table) {
    if (table.GetLocale().language.empty()) {
        return false;
    }
    const auto existing = std::find_if(m_tables.begin(), m_tables.end(),
                                       [&table](const StringTable& candidate) {
                                           return candidate.GetLocale() == table.GetLocale();
                                       });
    if (existing != m_tables.end()) {
        *existing = std::move(table);
        return true;
    }
    m_tables.push_back(std::move(table));
    return true;
}

void LocalizationService::SetActiveLocale(Locale locale) {
    m_activeLocale = std::move(locale);
}

void LocalizationService::SetFallbackLocale(Locale locale) {
    m_fallbackLocale = std::move(locale);
}

const StringTable* LocalizationService::FindTable(const Locale& locale) const noexcept {
    if (locale.language.empty()) {
        return nullptr;
    }
    const auto iterator = std::find_if(m_tables.cbegin(), m_tables.cend(),
                                       [&locale](const StringTable& table) {
                                           return table.GetLocale() == locale;
                                       });
    return iterator == m_tables.cend() ? nullptr : &*iterator;
}

const std::string* LocalizationService::Resolve(const std::string_view key) const noexcept {
    if (key.empty()) {
        return nullptr;
    }
    // Most specific first: the region-qualified table, then the language alone, then the fallback
    // locale. A region table holds only what actually differs from its language - "color" versus
    // "colour" - so falling through to the language table is the normal path, not an error path.
    const Locale chain[] = {
        m_activeLocale,
        Locale{m_activeLocale.language, ""},
        m_fallbackLocale,
        Locale{m_fallbackLocale.language, ""},
    };
    for (const Locale& locale : chain) {
        if (const StringTable* const table = FindTable(locale); table != nullptr) {
            if (const std::string* const translation = table->FindTranslation(key);
                translation != nullptr) {
                return translation;
            }
        }
    }
    return nullptr;
}

std::string LocalizationService::Translate(const std::string_view key) const {
    const std::string* const translation = Resolve(key);
    // The key itself is the last resort. A blank label reads as a broken build; a visible key reads
    // as a missing translation, which is what it is, and tells whoever sees it exactly what to add.
    return translation != nullptr ? *translation : std::string{key};
}

bool LocalizationService::HasTranslation(const std::string_view key) const noexcept {
    return Resolve(key) != nullptr;
}

} // namespace UVE::Localization
