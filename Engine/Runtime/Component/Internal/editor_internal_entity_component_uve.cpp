// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/editor_internal_entity_component_uve.h"

namespace UVE::Scene {

bool IsEditorInternalEntityComponentValidUVE(
    const EditorInternalEntityComponentUVE& /*component*/) noexcept {
    return true;
}

bool IsEditorInternalEntityTagPresentUVE(
    const EditorInternalEntityComponentUVE* const component) noexcept {
    return component != nullptr;
}

bool ShouldIncludeInDocumentHierarchyUVE(
    const EditorInternalEntityComponentUVE* const component) noexcept {
    return !IsEditorInternalEntityTagPresentUVE(component);
}

bool ShouldSerializeDocumentEntityUVE(
    const EditorInternalEntityComponentUVE* const component) noexcept {
    return !IsEditorInternalEntityTagPresentUVE(component);
}

bool ShouldClearWithDocumentSceneUVE(
    const EditorInternalEntityComponentUVE* const component) noexcept {
    return !IsEditorInternalEntityTagPresentUVE(component);
}

} // namespace UVE::Scene
