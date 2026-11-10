// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Text and Localization Pipeline.
// Provides string tables, locale registry, parameter formatting, and runtime language switching.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace prism {

class StringTable {
public:
  void set(std::string key, std::string value);
  std::string get(const std::string& key, const std::string& fallback = "") const;
  bool contains(const std::string& key) const;
  void clear();

  // Load key-value translation pairs from JSON string or file
  bool loadFromJson(const std::string& json);
  bool loadFromFile(const std::string& filePath);

  const std::unordered_map<std::string, std::string>& all() const { return m_strings; }

private:
  std::unordered_map<std::string, std::string> m_strings;
};

class LocalizationService {
public:
  static LocalizationService& get();

  void registerLocale(const std::string& localeCode, std::shared_ptr<StringTable> table);
  bool loadLocaleFile(const std::string& localeCode, const std::string& filePath);
  void setLocale(const std::string& localeCode);
  const std::string& currentLocale() const { return m_currentLocale; }

  std::vector<std::string> availableLocales() const;

  // Retrieve localized string with fallback
  std::string get(const std::string& key, const std::string& fallback = "") const;

  // Substitute {0}, {1}, etc. with arguments
  std::string format(const std::string& key, const std::vector<std::string>& args) const;

  // Pluralization selector
  std::string formatPlural(const std::string& keyZero, const std::string& keyOne, const std::string& keyMany, int count) const;

  std::function<void(const std::string& newLocale)> onLocaleChanged;

private:
  LocalizationService();
  void registerDefaults();

  std::unordered_map<std::string, std::shared_ptr<StringTable>> m_tables;
  std::string m_currentLocale = "en-US";
  std::string m_fallbackLocale = "en-US";
};

// Global inline helpers
inline std::string loc(const std::string& key, const std::string& fallback = "") {
  return LocalizationService::get().get(key, fallback.empty() ? key : fallback);
}

inline std::string locFmt(const std::string& key, const std::vector<std::string>& args) {
  return LocalizationService::get().format(key, args);
}

#define LOC(key) ::prism::loc(key)
#define LOC_FMT(key, ...) ::prism::locFmt(key, {__VA_ARGS__})

} // namespace prism
