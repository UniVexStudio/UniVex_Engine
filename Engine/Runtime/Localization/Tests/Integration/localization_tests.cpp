// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "localization.h"

#include <gtest/gtest.h>

namespace UVE::Localization::Tests {
namespace {

[[nodiscard]] StringTable MakeTable(const Locale& locale,
                                          const std::vector<std::pair<std::string, std::string>>& entries) {
    StringTable table{locale};
    for (const auto& [key, value] : entries) {
        EXPECT_TRUE(table.SetTranslation(key, value));
    }
    return table;
}

TEST(LocaleTest, TryParseLocale_AcceptsBothSeparatorsAndNormalizesCase) {
    // "en-US" and "en_US" are the same locale written by two conventions in circulation. Treating
    // them as different would create a table nothing ever falls back to.
    const std::optional<Locale> hyphen = TryParseLocale("EN-us");
    const std::optional<Locale> underscore = TryParseLocale("en_US");
    ASSERT_TRUE(hyphen.has_value());
    ASSERT_TRUE(underscore.has_value());
    EXPECT_EQ(*hyphen, *underscore);
    EXPECT_EQ(hyphen->language, "en");
    EXPECT_EQ(hyphen->region, "US");
    EXPECT_EQ(hyphen->ToTag(), "en-US");

    const std::optional<Locale> languageOnly = TryParseLocale("fil");
    ASSERT_TRUE(languageOnly.has_value());
    EXPECT_TRUE(languageOnly->region.empty());
    EXPECT_EQ(languageOnly->ToTag(), "fil");

    const std::optional<Locale> numericRegion = TryParseLocale("es-419");
    ASSERT_TRUE(numericRegion.has_value());
    EXPECT_EQ(numericRegion->language, "es");
    EXPECT_EQ(numericRegion->region, "419");
    EXPECT_EQ(numericRegion->ToTag(), "es-419");

    const std::optional<Locale> scriptAndRegion = TryParseLocale("zh_Hans-cn");
    ASSERT_TRUE(scriptAndRegion.has_value());
    EXPECT_EQ(scriptAndRegion->language, "zh");
    EXPECT_EQ(scriptAndRegion->region, "HANS-CN");
    EXPECT_EQ(scriptAndRegion->ToTag(), "zh-HANS-CN");
}

TEST(LocaleTest, TryParseLocale_RejectsWhatIsNotALocale) {
    EXPECT_FALSE(TryParseLocale("").has_value());
    EXPECT_FALSE(TryParseLocale("-US").has_value());
    EXPECT_FALSE(TryParseLocale("en-").has_value());
    EXPECT_FALSE(TryParseLocale("en-U5").has_value());
    EXPECT_FALSE(TryParseLocale("123").has_value());
    EXPECT_FALSE(TryParseLocale(std::string(64U, 'x')).has_value());
}

TEST(StringTableTest, SetTranslation_ReplacesInPlaceAndRefusesWhatCannotBeLookedUp) {
    StringTable table{Locale{"en", ""}};
    EXPECT_TRUE(table.SetTranslation("menu.play", "Play"));
    EXPECT_EQ(table.GetTranslationCount(), 1U);

    // Replacing does not grow the table; a key means one translation.
    EXPECT_TRUE(table.SetTranslation("menu.play", "Start"));
    EXPECT_EQ(table.GetTranslationCount(), 1U);
    ASSERT_NE(table.FindTranslation("menu.play"), nullptr);
    EXPECT_EQ(*table.FindTranslation("menu.play"), "Start");

    EXPECT_FALSE(table.SetTranslation("", "Nothing"));
    EXPECT_FALSE(table.SetTranslation(std::string(kMaximumTranslationKeyBytes + 1U, 'k'), "x"));
    EXPECT_FALSE(table.SetTranslation("key", std::string(kMaximumTranslationValueBytes + 1U, 'v')));
    EXPECT_EQ(table.GetTranslationCount(), 1U);
}

TEST(LocalizationServiceTest, Translate_WalksTheFallbackChainMostSpecificFirst) {
    LocalizationService service;
    EXPECT_TRUE(service.AddStringTable(MakeTable(Locale{"en", ""},
                                                       {{"menu.play", "Play"}, {"ui.color", "Color"}})));
    EXPECT_TRUE(service.AddStringTable(MakeTable(Locale{"en", "GB"}, {{"ui.color", "Colour"}})));
    EXPECT_TRUE(service.AddStringTable(MakeTable(Locale{"fil", ""}, {{"menu.play", "Maglaro"}})));

    service.SetActiveLocale(Locale{"en", "GB"});
    // The region table holds only what actually differs from its language, so falling through to
    // the language table is the normal path rather than an error path.
    EXPECT_EQ(service.Translate("ui.color"), "Colour");
    EXPECT_EQ(service.Translate("menu.play"), "Play");

    service.SetActiveLocale(Locale{"fil", ""});
    EXPECT_EQ(service.Translate("menu.play"), "Maglaro");
    // Nothing in Filipino for this key, so the fallback locale answers rather than the label
    // coming out blank.
    EXPECT_EQ(service.Translate("ui.color"), "Color");
}

TEST(LocalizationServiceTest, Translate_ReturnsTheKeyRatherThanAnEmptyLabel) {
    LocalizationService service;
    EXPECT_TRUE(service.AddStringTable(MakeTable(Locale{"en", ""}, {{"menu.play", "Play"}})));

    // A blank label reads as a broken build; a visible key reads as a missing translation, which is
    // what it is, and says exactly what needs adding.
    EXPECT_EQ(service.Translate("menu.quit"), "menu.quit");
    EXPECT_FALSE(service.HasTranslation("menu.quit"));
    EXPECT_TRUE(service.HasTranslation("menu.play"));
}

TEST(LocalizationServiceTest, SelectingALocaleWithNoTableIsNotAnError) {
    LocalizationService service;
    EXPECT_TRUE(service.AddStringTable(MakeTable(Locale{"en", ""}, {{"menu.play", "Play"}})));

    // Exactly the state a build is in while a translation is still being written: the locale is
    // selectable, and everything falls back until entries start arriving.
    service.SetActiveLocale(Locale{"ja", "JP"});
    EXPECT_EQ(service.Translate("menu.play"), "Play");
    EXPECT_EQ(service.GetActiveLocale().ToTag(), "ja-JP");
}

TEST(LocalizationServiceTest, AddStringTable_ReplacesOneLocalesTableAndRefusesALocalelessOne) {
    LocalizationService service;
    EXPECT_TRUE(service.AddStringTable(MakeTable(Locale{"en", ""}, {{"menu.play", "Play"}})));
    EXPECT_TRUE(service.AddStringTable(MakeTable(Locale{"en", ""}, {{"menu.play", "Start"}})));
    EXPECT_EQ(service.GetStringTableCount(), 1U);
    EXPECT_EQ(service.Translate("menu.play"), "Start");

    // A table with no language could never be selected or fallen back to, so installing one would
    // only ever hide a mistake.
    EXPECT_FALSE(service.AddStringTable(StringTable{Locale{"", "US"}}));
    EXPECT_EQ(service.GetStringTableCount(), 1U);
}

TEST(StringTableJsonTest, TryParseStringTableJson_ReadsAFlatObjectOfStrings) {
    const std::optional<StringTable> table = TryParseStringTableJson(
        Locale{"fil", ""}, R"({"Play": "Maglaro", "menu.quit": "Umalis", "note": ""})");
    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->GetLocale().language, "fil");
    EXPECT_EQ(table->GetTranslationCount(), 3U);
    ASSERT_NE(table->FindTranslation("menu.quit"), nullptr);
    EXPECT_EQ(*table->FindTranslation("menu.quit"), "Umalis");
    // An empty value is a legitimate translation (a string deliberately blanked in one language).
    ASSERT_NE(table->FindTranslation("note"), nullptr);
    EXPECT_TRUE(table->FindTranslation("note")->empty());
}

TEST(StringTableJsonTest, TryParseStringTableJson_RejectsTheWholeFileRatherThanShippingHoles) {
    const Locale locale{"fil", ""};
    EXPECT_FALSE(TryParseStringTableJson(locale, "{not json").has_value());
    EXPECT_FALSE(TryParseStringTableJson(locale, R"(["Play", "Maglaro"])").has_value());
    // One bad entry sinks the file: a table that dropped it silently would ship with a hole in it
    // and nothing to say where.
    EXPECT_FALSE(TryParseStringTableJson(locale, R"({"Play": "Maglaro", "count": 3})").has_value());
    EXPECT_FALSE(TryParseStringTableJson(locale, R"({"": "empty key"})").has_value());
    const std::string oversizedValue = R"({"key": ")" + std::string(kMaximumTranslationValueBytes + 1U, 'v') +
                                       R"("})";
    EXPECT_FALSE(TryParseStringTableJson(locale, oversizedValue).has_value());
}

} // namespace
} // namespace UVE::Localization::Tests
