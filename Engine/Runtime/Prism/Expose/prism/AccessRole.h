// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - accessibility roles (semantic meaning of a widget, independent of how it is drawn).
#pragma once
namespace prism {
enum class AccessRole {
  Generic, Window, Group, Label, Button, ToggleButton, CheckBox, Slider, ComboBox, TextBox, ProgressBar,
  Tab, TabList, Menu, MenuItem, Table, Row, ColumnHeader, Cell, Tree, TreeItem, List, ListItem, Separator, Image, Canvas
};
enum class AccessEvent { FocusChanged, NameChanged, ValueChanged, StateChanged, StructureChanged };
} // namespace prism
