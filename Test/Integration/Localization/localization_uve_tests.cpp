// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/localization/localization_uve.h"

#include <gtest/gtest.h>

namespace UVE::Localization::Tests {
namespace {

[[nodiscard]] StringTableUVE MakeTableUVE(const LocaleUVE& locale,
                                          const std::vector<std::pair<std::string, std::string>>& entries) {
    StringTableUVE table{locale};
    for (const auto& [key, value] : entries) {
        EXPECT_TRUE(table.SetTranslationUVE(key, value));
    }
    return table;
}

TEST(LocaleUVETest, TryParseLocaleUVE_AcceptsBothSeparatorsAndNormalizesCase) {
    // "en-US" and "en_US" are the same locale written by two conventions in circulation. Treating
    // them as different would create a table nothing ever falls back to.
    const std::optional<LocaleUVE> hyphen = TryParseLocaleUVE("EN-us");
    const std::optional<LocaleUVE> underscore = TryParseLocaleUVE("en_US");
    ASSERT_TRUE(hyphen.has_value());
    ASSERT_TRUE(underscore.has_value());
    EXPECT_EQ(*hyphen, *underscore);
    EXPECT_EQ(hyphen->language, "en");
    EXPECT_EQ(hyphen->region, "US");
    EXPECT_EQ(hyphen->ToTagUVE(), "en-US");

    const std::optional<LocaleUVE> languageOnly = TryParseLocaleUVE("fil");
    ASSERT_TRUE(languageOnly.has_value());
    EXPECT_TRUE(languageOnly->region.empty());
    EXPECT_EQ(languageOnly->ToTagUVE(), "fil");
}

TEST(LocaleUVETest, TryParseLocaleUVE_RejectsWhatIsNotALocale) {
    EXPECT_FALSE(TryParseLocaleUVE("").has_value());
    EXPECT_FALSE(TryParseLocaleUVE("-US").has_value());
    EXPECT_FALSE(TryParseLocaleUVE("en-").has_value());
    EXPECT_FALSE(TryParseLocaleUVE("en-U5").has_value());
    EXPECT_FALSE(TryParseLocaleUVE("123").has_value());
    EXPECT_FALSE(TryParseLocaleUVE(std::string(64U, 'x')).has_value());
}

TEST(StringTableUVETest, SetTranslationUVE_ReplacesInPlaceAndRefusesWhatCannotBeLookedUp) {
    StringTableUVE table{LocaleUVE{"en", ""}};
    EXPECT_TRUE(table.SetTranslationUVE("menu.play", "Play"));
    EXPECT_EQ(table.GetTranslationCountUVE(), 1U);

    // Replacing does not grow the table; a key means one translation.
    EXPECT_TRUE(table.SetTranslationUVE("menu.play", "Start"));
    EXPECT_EQ(table.GetTranslationCountUVE(), 1U);
    ASSERT_NE(table.FindTranslationUVE("menu.play"), nullptr);
    EXPECT_EQ(*table.FindTranslationUVE("menu.play"), "Start");

    EXPECT_FALSE(table.SetTranslationUVE("", "Nothing"));
    EXPECT_FALSE(table.SetTranslationUVE(std::string(kMaximumTranslationKeyBytesUVE + 1U, 'k'), "x"));
    EXPECT_FALSE(table.SetTranslationUVE("key", std::string(kMaximumTranslationValueBytesUVE + 1U, 'v')));
    EXPECT_EQ(table.GetTranslationCountUVE(), 1U);
}

TEST(LocalizationServiceUVETest, TranslateUVE_WalksTheFallbackChainMostSpecificFirst) {
    LocalizationServiceUVE service;
    EXPECT_TRUE(service.AddStringTableUVE(MakeTableUVE(LocaleUVE{"en", ""},
                                                       {{"menu.play", "Play"}, {"ui.color", "Color"}})));
    EXPECT_TRUE(service.AddStringTableUVE(MakeTableUVE(LocaleUVE{"en", "GB"}, {{"ui.color", "Colour"}})));
    EXPECT_TRUE(service.AddStringTableUVE(MakeTableUVE(LocaleUVE{"fil", ""}, {{"menu.play", "Maglaro"}})));

    service.SetActiveLocaleUVE(LocaleUVE{"en", "GB"});
    // The region table holds only what actually differs from its language, so falling through to
    // the language table is the normal path rather than an error path.
    EXPECT_EQ(service.TranslateUVE("ui.color"), "Colour");
    EXPECT_EQ(service.TranslateUVE("menu.play"), "Play");

    service.SetActiveLocaleUVE(LocaleUVE{"fil", ""});
    EXPECT_EQ(service.TranslateUVE("menu.play"), "Maglaro");
    // Nothing in Filipino for this key, so the fallback locale answers rather than the label
    // coming out blank.
    EXPECT_EQ(service.TranslateUVE("ui.color"), "Color");
}

TEST(LocalizationServiceUVETest, TranslateUVE_ReturnsTheKeyRatherThanAnEmptyLabel) {
    LocalizationServiceUVE service;
    EXPECT_TRUE(service.AddStringTableUVE(MakeTableUVE(LocaleUVE{"en", ""}, {{"menu.play", "Play"}})));

    // A blank label reads as a broken build; a visible key reads as a missing translation, which is
    // what it is, and says exactly what needs adding.
    EXPECT_EQ(service.TranslateUVE("menu.quit"), "menu.quit");
    EXPECT_FALSE(service.HasTranslationUVE("menu.quit"));
    EXPECT_TRUE(service.HasTranslationUVE("menu.play"));
}

TEST(LocalizationServiceUVETest, SelectingALocaleWithNoTableIsNotAnError) {
    LocalizationServiceUVE service;
    EXPECT_TRUE(service.AddStringTableUVE(MakeTableUVE(LocaleUVE{"en", ""}, {{"menu.play", "Play"}})));

    // Exactly the state a build is in while a translation is still being written: the locale is
    // selectable, and everything falls back until entries start arriving.
    service.SetActiveLocaleUVE(LocaleUVE{"ja", "JP"});
    EXPECT_EQ(service.TranslateUVE("menu.play"), "Play");
    EXPECT_EQ(service.GetActiveLocaleUVE().ToTagUVE(), "ja-JP");
}

TEST(LocalizationServiceUVETest, AddStringTableUVE_ReplacesOneLocalesTableAndRefusesALocalelessOne) {
    LocalizationServiceUVE service;
    EXPECT_TRUE(service.AddStringTableUVE(MakeTableUVE(LocaleUVE{"en", ""}, {{"menu.play", "Play"}})));
    EXPECT_TRUE(service.AddStringTableUVE(MakeTableUVE(LocaleUVE{"en", ""}, {{"menu.play", "Start"}})));
    EXPECT_EQ(service.GetStringTableCountUVE(), 1U);
    EXPECT_EQ(service.TranslateUVE("menu.play"), "Start");

    // A table with no language could never be selected or fallen back to, so installing one would
    // only ever hide a mistake.
    EXPECT_FALSE(service.AddStringTableUVE(StringTableUVE{LocaleUVE{"", "US"}}));
    EXPECT_EQ(service.GetStringTableCountUVE(), 1U);
}

TEST(StringTableJsonUVETest, TryParseStringTableJsonUVE_ReadsAFlatObjectOfStrings) {
    const std::optional<StringTableUVE> table = TryParseStringTableJsonUVE(
        LocaleUVE{"fil", ""}, R"({"Play": "Maglaro", "menu.quit": "Umalis", "note": ""})");
    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->GetLocaleUVE().language, "fil");
    EXPECT_EQ(table->GetTranslationCountUVE(), 3U);
    ASSERT_NE(table->FindTranslationUVE("menu.quit"), nullptr);
    EXPECT_EQ(*table->FindTranslationUVE("menu.quit"), "Umalis");
    // An empty value is a legitimate translation (a string deliberately blanked in one language).
    ASSERT_NE(table->FindTranslationUVE("note"), nullptr);
    EXPECT_TRUE(table->FindTranslationUVE("note")->empty());
}

TEST(StringTableJsonUVETest, TryParseStringTableJsonUVE_RejectsTheWholeFileRatherThanShippingHoles) {
    const LocaleUVE locale{"fil", ""};
    EXPECT_FALSE(TryParseStringTableJsonUVE(locale, "{not json").has_value());
    EXPECT_FALSE(TryParseStringTableJsonUVE(locale, R"(["Play", "Maglaro"])").has_value());
    // One bad entry sinks the file: a table that dropped it silently would ship with a hole in it
    // and nothing to say where.
    EXPECT_FALSE(TryParseStringTableJsonUVE(locale, R"({"Play": "Maglaro", "count": 3})").has_value());
    EXPECT_FALSE(TryParseStringTableJsonUVE(locale, R"({"": "empty key"})").has_value());
    const std::string oversizedValue = R"({"key": ")" + std::string(kMaximumTranslationValueBytesUVE + 1U, 'v') +
                                       R"("})";
    EXPECT_FALSE(TryParseStringTableJsonUVE(locale, oversizedValue).has_value());
}

} // namespace
} // namespace UVE::Localization::Tests
