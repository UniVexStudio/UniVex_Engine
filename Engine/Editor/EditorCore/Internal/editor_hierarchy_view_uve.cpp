// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_hierarchy_view_uve.h"

namespace UVE::Editor {

bool ShouldDrawHierarchyEyeUVE(const HierarchyVisibilityColumnUVE mode, const bool rowHovered,
                               const bool nodeVisible) noexcept {
    switch (mode) {
        case HierarchyVisibilityColumnUVE::Always:
            return true;
        case HierarchyVisibilityColumnUVE::OnHover:
            return rowHovered || !nodeVisible;
        case HierarchyVisibilityColumnUVE::Hidden:
            return false;
    }
    return true;
}

} // namespace UVE::Editor
