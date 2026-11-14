// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/Commands.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace prism {
namespace {

const char* kNamed[] = {"", "Backspace", "Tab", "Enter", "Escape", "Space", "Left", "Right", "Up", "Down", "Home", "End",
                        "PageUp", "PageDown", "Delete"};

std::string keyName(Key k) {
  int i = int(k);
  if (i <= int(Key::Delete)) return kNamed[i];
  if (k >= Key::A && k <= Key::Z) return std::string(1, char('A' + (i - int(Key::A))));
  if (k >= Key::N0 && k <= Key::N9) return std::string(1, char('0' + (i - int(Key::N0))));
  if (k >= Key::F1 && k <= Key::F12) return "F" + std::to_string(i - int(Key::F1) + 1);
  return "";
}

std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool keyFromName(const std::string& name, Key& out) {
  std::string n = lower(name);
  for (int i = int(Key::A); i <= int(Key::F12); i++) if (lower(keyName(Key(i))) == n) { out = Key(i); return true; }
  for (int i = int(Key::Backspace); i <= int(Key::Delete); i++) if (lower(keyName(Key(i))) == n) { out = Key(i); return true; }
  return false;
}

constexpr uint8_t kMask = ModCtrl | ModAlt | ModShift;

} // namespace

std::string Chord::str() const {
  if (!valid()) return "";
  std::string s;
  if (mods & ModCtrl) s += "Ctrl+";
  if (mods & ModShift) s += "Shift+";
  if (mods & ModAlt) s += "Alt+";
  return s + keyName(key);
}

bool Chord::parse(const std::string& text, Chord& out) {
  out = {};
  std::string t = text;
  t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c) { return std::isspace(c); }), t.end());
  if (t.empty()) return true;
  std::vector<std::string> parts;
  std::stringstream ss(t);
  std::string p;
  while (std::getline(ss, p, '+')) parts.push_back(p);
  if (!t.empty() && t.back() == '+') return false;
  uint8_t mods = 0;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    std::string m = lower(parts[i]);
    if (m == "ctrl" || m == "cmd") mods |= ModCtrl;
    else if (m == "shift") mods |= ModShift;
    else if (m == "alt") mods |= ModAlt;
    else return false;
  }
  Key k;
  if (parts.empty() || !keyFromName(parts.back(), k)) return false;
  out.key = k;
  out.mods = mods;
  return true;
}

Command& CommandList::add(std::string id, std::string label, const std::string& chord, std::function<void()> run, Icon icon, std::string category) {
  Command c;
  c.id = std::move(id);
  c.label = std::move(label);
  c.category = std::move(category);
  c.icon = icon;
  c.run = std::move(run);
  Chord::parse(chord, c.defaultChord);
  c.chord = c.defaultChord;
  m_cmds.push_back(std::move(c));
  return m_cmds.back();
}

Command* CommandList::find(const std::string& id) {
  for (auto& c : m_cmds) if (c.id == id) return &c;
  return nullptr;
}
const Command* CommandList::find(const std::string& id) const {
  for (auto& c : m_cmds) if (c.id == id) return &c;
  return nullptr;
}

bool CommandList::execute(const std::string& id) {
  Command* c = find(id);
  if (!c || !c->run || !c->isEnabled()) return false;
  c->run();
  return true;
}

bool CommandList::dispatch(Key key, uint8_t mods) {
  Chord in{key, uint8_t(mods & kMask)};
  for (auto& c : m_cmds)
    if (c.chord.valid() && c.chord == in && c.isEnabled() && c.run) { c.run(); return true; }
  return false;
}

std::string CommandList::conflict(const Chord& ch, const std::string& exceptId) const {
  if (!ch.valid()) return "";
  for (auto& c : m_cmds) if (c.id != exceptId && c.chord == ch) return c.id;
  return "";
}

bool CommandList::rebind(const std::string& id, Chord ch, std::string* conflictId) {
  Command* c = find(id);
  if (!c) return false;
  ch.mods &= kMask;
  std::string other = conflict(ch, id);
  if (!other.empty()) { if (conflictId) *conflictId = other; return false; }
  if (!ch.valid()) ch = {};
  if (c->chord == ch) return true;
  c->chord = ch;
  if (onChanged) onChanged();
  return true;
}

void CommandList::resetAll() {
  for (auto& c : m_cmds) c.chord = c.defaultChord;
  if (onChanged) onChanged();
}

std::string CommandList::serialize() const {
  std::string s;
  for (auto& c : m_cmds)
    if (!(c.chord == c.defaultChord)) s += c.id + "=" + c.chord.str() + "\n";
  return s;
}

int CommandList::deserialize(const std::string& text) {
  std::stringstream ss(text);
  std::string line;
  std::vector<std::pair<Command*, Chord>> parsed;
  while (std::getline(ss, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
    Command* c = find(line.substr(0, eq));
    Chord ch;
    if (!c || !Chord::parse(line.substr(eq + 1), ch)) continue;
    parsed.emplace_back(c, ch);
  }
  // Apply as a set so swaps (A<->B) are not rejected as conflicts halfway through.
  for (auto& [c, ch] : parsed) c->chord = {};
  int n = 0;
  for (auto& [c, ch] : parsed) {
    if (!ch.valid() || conflict(ch, c->id).empty()) { c->chord = ch; n++; }
    else c->chord = c->defaultChord;
  }
  if (n && onChanged) onChanged();
  return n;
}

std::string CommandList::chordText(const std::string& id) const {
  const Command* c = find(id);
  return c ? c->chord.str() : "";
}

MenuItem CommandList::menuItem(const std::string& id) const {
  const Command* c = find(id);
  if (!c) return MenuItem::Item(id, {});
  MenuItem m = MenuItem::Item(c->label, c->run, c->chord.str(), c->icon);
  if (!c->isEnabled()) m.disabled();
  return m;
}

std::string CommandList::tip(const std::string& id, const std::string& text) const {
  std::string k = chordText(id);
  return k.empty() ? text : text + " (" + k + ")";
}

} // namespace prism
