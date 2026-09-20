// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/editor_description_component_uve.h"

#include <string>

namespace UVE::Scene {

bool IsEditorDescriptionComponentValidUVE(const EditorDescriptionComponentUVE& component) noexcept {
    // Embedded nulls are rejected for the same reason NameComponentUVE rejects them: the string
    // crosses into JSON and into C-string UI calls, both of which would silently truncate at the
    // null and leave the author's text quietly cut in half.
    return component.description.size() <= kMaximumEditorDescriptionBytesUVE &&
           component.description.find('\0') == std::string::npos;
}

} // namespace UVE::Scene
