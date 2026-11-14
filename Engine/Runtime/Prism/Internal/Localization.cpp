// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Text and Localization Pipeline Implementation.
#include "prism/Localization.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace prism {

void StringTable::set(std::string key, std::string value) {
  m_strings[std::move(key)] = std::move(value);
}

std::string StringTable::get(const std::string& key, const std::string& fallback) const {
  auto it = m_strings.find(key);
  if (it != m_strings.end()) {
    return it->second;
  }
  return fallback;
}

bool StringTable::contains(const std::string& key) const {
  return m_strings.find(key) != m_strings.end();
}

void StringTable::clear() {
  m_strings.clear();
}

bool StringTable::loadFromJson(const std::string& json) {
  size_t i = 0;
  auto ws = [&] {
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) i++;
  };
  auto readString = [&](std::string& out) -> bool {
    if (i >= json.size() || json[i] != '"') return false;
    i++;
    out.clear();
    while (i < json.size() && json[i] != '"') {
      if (json[i] == '\\' && i + 1 < json.size()) {
        i++;
        if (json[i] == 'n') out += '\n';
        else if (json[i] == 't') out += '\t';
        else if (json[i] == '"') out += '"';
        else if (json[i] == '\\') out += '\\';
        else out += json[i];
        i++;
      } else {
        out += json[i++];
      }
    }
    if (i >= json.size()) return false;
    i++; // Skip closing quote
    return true;
  };

  ws();
  if (i >= json.size() || json[i] != '{') return false;
  i++;

  while (i < json.size()) {
    ws();
    if (i < json.size() && json[i] == '}') {
      i++;
      return true;
    }

    std::string key;
    if (!readString(key)) return false;

    ws();
    if (i >= json.size() || json[i] != ':') return false;
    i++;

    ws();
    std::string val;
    if (!readString(val)) return false;

    m_strings[std::move(key)] = std::move(val);

    ws();
    if (i < json.size() && json[i] == ',') {
      i++;
      continue;
    }
    if (i < json.size() && json[i] == '}') {
      i++;
      return true;
    }
    return false;
  }
  return true;
}

bool StringTable::loadFromFile(const std::string& filePath) {
  std::ifstream f(filePath);
  if (!f.is_open()) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  return loadFromJson(ss.str());
}

LocalizationService& LocalizationService::get() {
  static LocalizationService s_instance;
  return s_instance;
}

LocalizationService::LocalizationService() {
  registerDefaults();
}

void LocalizationService::registerDefaults() {
  // English (Default)
  auto en = std::make_shared<StringTable>();
  en->set("menu.file", "File");
  en->set("menu.edit", "Edit");
  en->set("menu.create", "Create");
  en->set("menu.view", "View");
  en->set("menu.tools", "Tools");
  en->set("menu.help", "Help");
  en->set("tab.hierarchy", "Hierarchy");
  en->set("tab.details", "Details");
  en->set("tab.console", "Console");
  en->set("tab.viewport", "Viewport");
  en->set("tab.assets", "Assets");
  en->set("tab.world", "World");
  en->set("tab.components", "PRISM Components");
  en->set("tab.statistics", "Statistics");
  en->set("tab.output", "Output");
  en->set("btn.play", "Play");
  en->set("btn.stop", "Stop");
  en->set("btn.clear", "Clear");
  en->set("status.ready", "Ready");
  en->set("msg.item_count", "Found {0} items");
  registerLocale("en-US", en);

  // Japanese
  auto ja = std::make_shared<StringTable>();
  ja->set("menu.file", "ファイル");
  ja->set("menu.edit", "編集");
  ja->set("menu.create", "作成");
  ja->set("menu.view", "表示");
  ja->set("menu.tools", "ツール");
  ja->set("menu.help", "ヘルプ");
  ja->set("tab.hierarchy", "階層");
  ja->set("tab.details", "詳細");
  ja->set("tab.console", "コンソール");
  ja->set("tab.viewport", "ビューポート");
  ja->set("tab.assets", "アセット");
  ja->set("tab.world", "ワールド");
  ja->set("tab.components", "PRISMコンポーネント");
  ja->set("tab.statistics", "統計");
  ja->set("tab.output", "出力");
  ja->set("btn.play", "再生");
  ja->set("btn.stop", "停止");
  ja->set("btn.clear", "クリア");
  ja->set("status.ready", "準備完了");
  ja->set("msg.item_count", "{0} 個のアイテムが見つかりました");
  registerLocale("ja-JP", ja);

  // German
  auto de = std::make_shared<StringTable>();
  de->set("menu.file", "Datei");
  de->set("menu.edit", "Bearbeiten");
  de->set("menu.create", "Erstellen");
  de->set("menu.view", "Ansicht");
  de->set("menu.tools", "Werkzeuge");
  de->set("menu.help", "Hilfe");
  de->set("tab.hierarchy", "Hierarchie");
  de->set("tab.details", "Details");
  de->set("tab.console", "Konsole");
  de->set("tab.viewport", "Ansichtsfenster");
  de->set("tab.assets", "Ressourcen");
  de->set("tab.world", "Welt");
  de->set("tab.components", "PRISM-Komponenten");
  de->set("tab.statistics", "Statistiken");
  de->set("tab.output", "Ausgabe");
  de->set("btn.play", "Wiedergabe");
  de->set("btn.stop", "Stopp");
  de->set("btn.clear", "Löschen");
  de->set("status.ready", "Bereit");
  de->set("msg.item_count", "{0} Elemente gefunden");
  registerLocale("de-DE", de);
}

void LocalizationService::registerLocale(const std::string& localeCode, std::shared_ptr<StringTable> table) {
  m_tables[localeCode] = std::move(table);
}

bool LocalizationService::loadLocaleFile(const std::string& localeCode, const std::string& filePath) {
  auto table = std::make_shared<StringTable>();
  if (!table->loadFromFile(filePath)) return false;
  registerLocale(localeCode, table);
  return true;
}

void LocalizationService::setLocale(const std::string& localeCode) {
  if (m_currentLocale != localeCode) {
    m_currentLocale = localeCode;
    if (onLocaleChanged) {
      onLocaleChanged(m_currentLocale);
    }
  }
}

std::vector<std::string> LocalizationService::availableLocales() const {
  std::vector<std::string> list;
  for (const auto& [code, _] : m_tables) {
    list.push_back(code);
  }
  std::sort(list.begin(), list.end());
  return list;
}

std::string LocalizationService::get(const std::string& key, const std::string& fallback) const {
  // 1. Try active locale
  auto it = m_tables.find(m_currentLocale);
  if (it != m_tables.end() && it->second->contains(key)) {
    return it->second->get(key);
  }

  // 2. Try fallback locale (e.g. en-US)
  if (m_currentLocale != m_fallbackLocale) {
    auto fit = m_tables.find(m_fallbackLocale);
    if (fit != m_tables.end() && fit->second->contains(key)) {
      return fit->second->get(key);
    }
  }

  // 3. Fallback string or key itself
  return fallback.empty() ? key : fallback;
}

std::string LocalizationService::format(const std::string& key, const std::vector<std::string>& args) const {
  std::string templateStr = get(key);
  std::string result;
  result.reserve(templateStr.size() + 32);

  for (size_t i = 0; i < templateStr.size();) {
    if (templateStr[i] == '{' && i + 2 < templateStr.size()) {
      size_t closePos = templateStr.find('}', i + 1);
      if (closePos != std::string::npos) {
        std::string numStr = templateStr.substr(i + 1, closePos - i - 1);
        bool allDigits = !numStr.empty() && std::all_of(numStr.begin(), numStr.end(), ::isdigit);
        if (allDigits) {
          int argIdx = std::stoi(numStr);
          if (argIdx >= 0 && argIdx < int(args.size())) {
            result += args[size_t(argIdx)];
            i = closePos + 1;
            continue;
          }
        }
      }
    }
    result += templateStr[i++];
  }
  return result;
}

std::string LocalizationService::formatPlural(const std::string& keyZero, const std::string& keyOne, const std::string& keyMany, int count) const {
  if (count == 0) return get(keyZero);
  if (count == 1) return get(keyOne);
  return get(keyMany);
}

} // namespace prism
