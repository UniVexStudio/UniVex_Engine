// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

#include "uve/window/window_desc_uve.h"

namespace UVE::Window {

/// Replaces {productName}, {projectName}, and {sceneName}. The scene token/suffix is populated
/// only during Editor Play when `appendSceneNameInEditorPlayMode` is enabled. Unknown tokens are
/// preserved as literal text; the result is bounded to GLFW's maximum title length.
[[nodiscard]] std::string FormatWindowTitleUVE(
    std::string_view format, std::string_view productName, std::string_view projectName,
    std::string_view sceneName, bool editorPlayMode, bool appendSceneNameInEditorPlayMode);

} // namespace UVE::Window
