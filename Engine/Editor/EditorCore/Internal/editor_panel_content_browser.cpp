// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Content Browser. One toolbar across the top (create, import, save, back/forward, the path,
// view settings); under it a sidebar with the pinned paths, the project's folder tree and the
// user's shelves, and beside that the filter and search row, the items, and a status line.
//
// What is shown is decided in editor_content_browser_model_uve (listing, search, history,
// shelves) so the rules are tested apart from the drawing, and the editor bridge lists the same
// items the panel draws.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "asset_import_queue.h"
#include "editor_content_catalogue.h"
#include "editor_content_browser_model.h"

namespace UVE::Editor {
namespace {

}

} // namespace UVE::Editor
