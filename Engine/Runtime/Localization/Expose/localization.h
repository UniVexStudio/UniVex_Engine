// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace UVE::Localization {

/// A BCP-47-shaped locale tag: a language subtag, optionally followed by a region.
///
/// Stored as two parts rather than one string because the fallback chain is built from them, and
/// re-splitting a tag at every lookup would put string parsing on the display path.
struct LocaleUVE final {
    /// Lowercase two- or three-letter language subtag ("en", "fil").
    std::string language;
    /// Uppercase region subtag ("US", "PH"), empty when the locale names only a language.
    std::string region;

    [[nodiscard]] bool operator==(const LocaleUVE&) const = default;

    /// "en-US", or "en" when there is no region.
    [[nodiscard]] std::string ToTagUVE() const;
};

inline constexpr std::size_t kMaximumLocaleSubtagBytesUVE = 8U;
inline constexpr std::size_t kMaximumTranslationKeyBytesUVE = 256U;
inline constexpr std::size_t kMaximumTranslationValueBytesUVE = 4096U;
inline constexpr std::size_t kMaximumTranslationsPerTableUVE = 8192U;

/// Parses "en", "en-US" or "en_US" into a locale. Returns nothing for a tag that is empty,
/// oversized, or shaped like something other than language[-region] - a malformed tag must not
/// silently become a locale nothing will ever have translations for.
[[nodiscard]] std::optional<LocaleUVE> TryParseLocaleUVE(std::string_view tag);

/// One locale's translations: keys to display strings.
class StringTableUVE final {
public:
    StringTableUVE() = default;
    explicit StringTableUVE(LocaleUVE locale) : m_locale(std::move(locale)) {}

    [[nodiscard]] const LocaleUVE& GetLocaleUVE() const noexcept { return m_locale; }

    /// Adds or replaces one translation. Returns false without mutation for an empty or oversized
    /// key, an oversized value, or a table already at capacity.
    [[nodiscard]] bool SetTranslationUVE(std::string key, std::string value);

    /// The translation for `key`, or null when this table has none.
    [[nodiscard]] const std::string* FindTranslationUVE(std::string_view key) const noexcept;

    [[nodiscard]] std::size_t GetTranslationCountUVE() const noexcept { return m_translations.size(); }

private:
    struct TranslationUVE final {
        std::string key;
        std::string value;
    };

    LocaleUVE m_locale;
    std::vector<TranslationUVE> m_translations;
};

/// Parses a string table from a JSON document: one flat object mapping each key to its display
/// string, e.g. {"Play": "Maglaro", "menu.quit": "Umalis"}.
///
/// All or nothing. Returns nothing - and yields no partial table - when the text is not valid JSON,
/// is not an object, holds a value that is not a string, or holds any entry StringTableUVE would
/// refuse (an empty or oversized key, an oversized value, too many entries). A table that silently
/// dropped the entries it did not like would ship a translation with holes in it and no sign of
/// where they were.
[[nodiscard]] std::optional<StringTableUVE> TryParseStringTableJsonUVE(LocaleUVE locale, std::string_view json);

/// Resolves a translation key against the active locale, falling back until something answers.
///
/// THE FALLBACK CHAIN, AND WHY IT IS NOT OPTIONAL. Asking for "menu.play" in en-US tries, in order:
/// the en-US table, the en table, the fallback locale's table, and finally the key itself. Without
/// that chain a build ships with blank labels wherever a translator has not caught up, which is a
/// worse failure than showing English: blank text looks like a broken build, untranslated text
/// looks like an unfinished translation. Returning the key last means a missing entry is visible to
/// whoever is testing instead of invisible until a player reports it.
///
/// Thread-safety: main-thread only, like the rest of the frame-facing services.
class LocalizationServiceUVE final {
public:
    LocalizationServiceUVE() = default;

    /// Installs or replaces the table for its own locale. A table whose locale has no language is
    /// rejected - it could never be selected or fallen back to.
    [[nodiscard]] bool AddStringTableUVE(StringTableUVE table);

    /// The locale lookups resolve against. Setting a locale with no installed table is allowed and
    /// is not an error: the chain simply falls through to the fallback locale, which is exactly
    /// what happens while a translation is still being written.
    void SetActiveLocaleUVE(LocaleUVE locale);
    [[nodiscard]] const LocaleUVE& GetActiveLocaleUVE() const noexcept { return m_activeLocale; }

    /// The last locale tried before the key itself. Defaults to "en".
    void SetFallbackLocaleUVE(LocaleUVE locale);
    [[nodiscard]] const LocaleUVE& GetFallbackLocaleUVE() const noexcept { return m_fallbackLocale; }

    /// Resolves `key` through the chain above. Never returns an empty string for a non-empty key.
    [[nodiscard]] std::string TranslateUVE(std::string_view key) const;

    /// True when the active chain actually has an entry for `key` - what a tool uses to report
    /// what is still untranslated, which TranslateUVE alone cannot answer because it always
    /// succeeds by design.
    [[nodiscard]] bool HasTranslationUVE(std::string_view key) const noexcept;

    [[nodiscard]] std::size_t GetStringTableCountUVE() const noexcept { return m_tables.size(); }

private:
    [[nodiscard]] const StringTableUVE* FindTableUVE(const LocaleUVE& locale) const noexcept;
    [[nodiscard]] const std::string* ResolveUVE(std::string_view key) const noexcept;

    LocaleUVE m_activeLocale{"en", ""};
    LocaleUVE m_fallbackLocale{"en", ""};
    std::vector<StringTableUVE> m_tables;
};

} // namespace UVE::Localization
