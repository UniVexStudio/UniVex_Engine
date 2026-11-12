// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - a menu with a search field: the "+ Add" menus of an editor (Unreal's Add / Place Actors).
#pragma once
#include "Menu.h"

namespace prism {

// Shows `items` in a popup headed by a search field that has the keyboard at once.
//   - With no query, each submenu is a group: its label as a header, its items below (deeper submenus
//     are flattened into their group). Top-level items with no submenu come first.
//   - Typing filters every item of every group into one list, best matches first, each with its group
//     on the right. Up / Down / Enter pick, Esc closes.
// Separators and disabled items are shown as they are; disabled items cannot be picked.
// `flyouts` (Unreal's Content Browser Add): with no query, each top-level submenu is one row that opens beside
// the menu instead of a group of rows; MenuItem::Header rows title the sections ("GET", "CREATE").
void showSearchMenu(std::vector<MenuItem> items, Rect anchor, PopupPlacement placement, std::string placeholder = "Search",
                    bool flyouts = false);

} // namespace prism
