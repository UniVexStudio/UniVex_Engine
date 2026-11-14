// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - command system: named actions with rebindable key chords. Menus, toolbars, tooltips and the
// keyboard-shortcut dialog are all generated from one CommandList, so a rebind is reflected everywhere.
#pragma once
#include "Icons.h"
#include "Menu.h"
#include <functional>
#include <string>
#include <vector>

namespace prism {

struct Chord {
  Key key = Key::Unknown;
  uint8_t mods = 0; // ModCtrl | ModShift | ModAlt
  bool valid() const { return key != Key::Unknown; }
  bool operator==(const Chord&) const = default;
  std::string str() const;                    // "Ctrl+Shift+Z", "" when unbound
  static bool parse(const std::string& text, Chord& out); // case-insensitive; "" parses to an unbound chord
};

struct Command {
  std::string id, label, category;
  Chord chord, defaultChord;
  Icon icon = Icon::None;
  bool hidden = false;                        // bound but not listed in the shortcut dialog
  std::function<void()> run;
  std::function<bool()> enabled;              // null = always enabled
  bool isEnabled() const { return !enabled || enabled(); }
};

class CommandList {
public:
  // `chord` is text such as "Ctrl+S" or "" for unbound; throws nothing - an unparsable default becomes unbound.
  Command& add(std::string id, std::string label, const std::string& chord, std::function<void()> run,
               Icon icon = Icon::None, std::string category = "");
  Command* find(const std::string& id);
  const Command* find(const std::string& id) const;
  const std::vector<Command>& all() const { return m_cmds; }

  bool execute(const std::string& id);                  // runs if enabled
  bool dispatch(Key key, uint8_t mods);                 // runs the enabled command bound to the chord
  // Id of another command already bound to `c` ("" if free).
  std::string conflict(const Chord& c, const std::string& exceptId) const;
  // Rebinds; refuses (returns false, sets *conflictId) if the chord belongs to another command. An invalid chord unbinds.
  bool rebind(const std::string& id, Chord c, std::string* conflictId = nullptr);
  void resetAll();

  // Persistence: only commands that differ from their defaults are written ("id=Ctrl+K" per line; "id=" = unbound).
  std::string serialize() const;
  int deserialize(const std::string& text);             // returns the number of bindings applied; unknown ids are ignored

  std::string chordText(const std::string& id) const;   // "" if unbound/unknown
  // Label + shortcut text + icon + enabled state, bound to the command.
  MenuItem menuItem(const std::string& id) const;
  // "Undo (Ctrl+Z)" - for tooltips.
  std::string tip(const std::string& id, const std::string& text) const;

  std::function<void()> onChanged;                      // fired after any binding change

private:
  std::vector<Command> m_cmds;
};

} // namespace prism
