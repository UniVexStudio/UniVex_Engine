// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/localization/localization_uve.h"

#include <algorithm>
#include <cctype>
#include <utility>

#include <nlohmann/json.hpp>

namespace UVE::Localization {
namespace {

[[nodiscard]] bool IsAsciiAlphaUVE(const char character) noexcept {
    return std::isalpha(static_cast<unsigned char>(character)) != 0;
}

[[nodiscard]] bool IsAlphaSubtagUVE(const std::string_view subtag) noexcept {
    return !subtag.empty() && subtag.size() <= kMaximumLocaleSubtagBytesUVE &&
           std::all_of(subtag.cbegin(), subtag.cend(), IsAsciiAlphaUVE);
}

[[nodiscard]] std::string ToLowerUVE(const std::string_view value) {
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

[[nodiscard]] std::string ToUpperUVE(const std::string_view value) {
    std::string result{value};
    std::transform(result.begin(), result.end(), result.begin(), [](const char character) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    });
    return result;
}

} // namespace

std::string LocaleUVE::ToTagUVE() const {
    return region.empty() ? language : language + "-" + region;
}

std::optional<LocaleUVE> TryParseLocaleUVE(const std::string_view tag) {
    if (tag.empty() || tag.size() > (kMaximumLocaleSubtagBytesUVE * 2U) + 1U) {
        return std::nullopt;
    }
    // Both separators are accepted because both are in circulation - BCP 47 writes "en-US" while
    // POSIX environments write "en_US", and a tag read from either place should mean the same
    // locale rather than silently becoming one nothing has translations for.
    const std::size_t separator = tag.find_first_of("-_");
    const std::string_view language = tag.substr(0U, separator);
    if (!IsAlphaSubtagUVE(language)) {
        return std::nullopt;
    }
    if (separator == std::string_view::npos) {
        return LocaleUVE{ToLowerUVE(language), ""};
    }
    const std::string_view region = tag.substr(separator + 1U);
    if (!IsAlphaSubtagUVE(region)) {
        return std::nullopt;
    }
    // Normalized on the way in - lowercase language, uppercase region - so "EN-us" and "en-US"
    // resolve to the same table instead of to two tables that never fall back to each other.
    return LocaleUVE{ToLowerUVE(language), ToUpperUVE(region)};
}

bool StringTableUVE::SetTranslationUVE(std::string key, std::string value) {
    if (key.empty() || key.size() > kMaximumTranslationKeyBytesUVE ||
        value.size() > kMaximumTranslationValueBytesUVE) {
        return false;
    }
    const auto existing = std::find_if(m_translations.begin(), m_translations.end(),
                                       [&key](const TranslationUVE& translation) {
                                           return translation.key == key;
                                       });
    if (existing != m_translations.end()) {
        existing->value = std::move(value);
        return true;
    }
    if (m_translations.size() >= kMaximumTranslationsPerTableUVE) {
        return false;
    }
    m_translations.push_back(TranslationUVE{std::move(key), std::move(value)});
    return true;
}

const std::string* StringTableUVE::FindTranslationUVE(const std::string_view key) const noexcept {
    const auto iterator = std::find_if(m_translations.cbegin(), m_translations.cend(),
                                       [key](const TranslationUVE& translation) {
                                           return translation.key == key;
                                       });
    return iterator == m_translations.cend() ? nullptr : &iterator->value;
}

std::optional<StringTableUVE> TryParseStringTableJsonUVE(LocaleUVE locale, const std::string_view json) {
    // Parsed without exceptions: a malformed translation file is an expected input, not an
    // exceptional one, and the caller is told through the empty result.
    const nlohmann::json document = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (document.is_discarded() || !document.is_object() ||
        document.size() > kMaximumTranslationsPerTableUVE) {
        return std::nullopt;
    }
    StringTableUVE table{std::move(locale)};
    for (const auto& [key, value] : document.items()) {
        if (!value.is_string() || !table.SetTranslationUVE(key, value.get<std::string>())) {
            return std::nullopt;
        }
    }
    return table;
}

bool LocalizationServiceUVE::AddStringTableUVE(StringTableUVE table) {
    if (table.GetLocaleUVE().language.empty()) {
        return false;
    }
    const auto existing = std::find_if(m_tables.begin(), m_tables.end(),
                                       [&table](const StringTableUVE& candidate) {
                                           return candidate.GetLocaleUVE() == table.GetLocaleUVE();
                                       });
    if (existing != m_tables.end()) {
        *existing = std::move(table);
        return true;
    }
    m_tables.push_back(std::move(table));
    return true;
}

void LocalizationServiceUVE::SetActiveLocaleUVE(LocaleUVE locale) {
    m_activeLocale = std::move(locale);
}

void LocalizationServiceUVE::SetFallbackLocaleUVE(LocaleUVE locale) {
    m_fallbackLocale = std::move(locale);
}

const StringTableUVE* LocalizationServiceUVE::FindTableUVE(const LocaleUVE& locale) const noexcept {
    if (locale.language.empty()) {
        return nullptr;
    }
    const auto iterator = std::find_if(m_tables.cbegin(), m_tables.cend(),
                                       [&locale](const StringTableUVE& table) {
                                           return table.GetLocaleUVE() == locale;
                                       });
    return iterator == m_tables.cend() ? nullptr : &*iterator;
}

const std::string* LocalizationServiceUVE::ResolveUVE(const std::string_view key) const noexcept {
    if (key.empty()) {
        return nullptr;
    }
    // Most specific first: the region-qualified table, then the language alone, then the fallback
    // locale. A region table holds only what actually differs from its language - "color" versus
    // "colour" - so falling through to the language table is the normal path, not an error path.
    const LocaleUVE chain[] = {
        m_activeLocale,
        LocaleUVE{m_activeLocale.language, ""},
        m_fallbackLocale,
        LocaleUVE{m_fallbackLocale.language, ""},
    };
    for (const LocaleUVE& locale : chain) {
        if (const StringTableUVE* const table = FindTableUVE(locale); table != nullptr) {
            if (const std::string* const translation = table->FindTranslationUVE(key);
                translation != nullptr) {
                return translation;
            }
        }
    }
    return nullptr;
}

std::string LocalizationServiceUVE::TranslateUVE(const std::string_view key) const {
    const std::string* const translation = ResolveUVE(key);
    // The key itself is the last resort. A blank label reads as a broken build; a visible key reads
    // as a missing translation, which is what it is, and tells whoever sees it exactly what to add.
    return translation != nullptr ? *translation : std::string{key};
}

bool LocalizationServiceUVE::HasTranslationUVE(const std::string_view key) const noexcept {
    return ResolveUVE(key) != nullptr;
}

} // namespace UVE::Localization
